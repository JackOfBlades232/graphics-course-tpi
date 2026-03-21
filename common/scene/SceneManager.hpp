#pragma once

#include <cstdint>
#include <filesystem>
#include <array>
#include <thread>
#include <atomic>

#include <utils/Common.hpp>

#include <glm/glm.hpp>
#include <tiny_gltf.h>
#include <etna/Buffer.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/PerFrameTransferHelper.hpp>
#include <etna/VertexInput.hpp>
#include <etna/GpuSharedResource.hpp>
#include <etna/DescriptorSet.hpp>

#include <lights.h>
#include <materials.h>
#include <geometry.h>
#include <draw.h>
#include <terrain.h>
#include <skybox.h>

struct RenderElement
{
  uint32_t vertexOffset;
  uint32_t indexOffset;
  uint32_t indexCount;
  MaterialId materialId = MaterialId::INVALID;
};

struct Mesh
{
  uint32_t firstRelem;
  uint32_t relemCount;
};

struct SceneMultiplexing
{
  glm::uvec3 dims = {1u, 1u, 1u};
  glm::vec3 offsets = {};
};

struct SceneShadowsSetup
{
  bool allocatePointShadowTextures = true;
  bool allocateSpotShadowTextures = true;
  bool allocateDirectionalShadowTextures = true;
};

enum class SceneTextureUploadStage
{
  INIT,
  LOADING_FROM_DISK,
  DONE_LOADING_FROM_DISK,
  UPLOADING_TO_GPU,
  DONE,
  FAILED
};

struct SceneTextureDesc
{
  TexId tid{TexId::INVALID};
  std::string uri{};
  vk::Format format{vk::Format::eUndefined};
  alignas(SceneTextureUploadStage) uint8_t uploadStageStorage[sizeof(SceneTextureUploadStage)]{};
  bool isCube = false;
  bool inited = false;
  struct As
  {
    struct Planar
    {
      uint32_t w, h;
      etna::AsyncImageUploadState gpuUploadState;
      std::vector<unsigned char> content;
    } planar;
    struct Cube
    {
      uint32_t side;
      std::array<etna::AsyncImageUploadState, 6> gpuUploadState;
      std::array<std::vector<unsigned char>, 6> content;
    } cube;
  } as;

  SceneTextureDesc() = default;
  SceneTextureDesc(const SceneTextureDesc&) = default;
  SceneTextureDesc(SceneTextureDesc&&) = default;
  SceneTextureDesc& operator=(const SceneTextureDesc&) = default;
  SceneTextureDesc& operator=(SceneTextureDesc&&) = default;

  auto& uploadStage() { return *(std::atomic<SceneTextureUploadStage>*)uploadStageStorage; }
  const auto& uploadStage() const
  {
    return *(const std::atomic<SceneTextureUploadStage>*)uploadStageStorage;
  }

  bool acqReady() const
  {
    return uploadStage().load(std::memory_order_acquire) == SceneTextureUploadStage::DONE;
  }

  void cleanup()
  {
    if (isCube)
      as.cube.content = {};
    else
      as.planar.content = {};
  }

  ~SceneTextureDesc()
  {
    if (inited)
      std::destroy_at(&uploadStage());
  }
};

using CsmCascades = std::array<etna::Image, CSM_CASCADE_COUNT>;

class SceneManager
{
public:
  explicit SceneManager(const etna::GpuWorkCount& wc);

  void selectScene(
    std::filesystem::path path,
    const SceneShadowsSetup& shadows_setup = {},
    const SceneMultiplexing& multiplex = {});

  bool canRender() const { return sceneDataUpload.done; }
  bool sceneFullyReady() const { return canRender() && texturesUploaded >= sceneTextures.size(); }

  // @TODO: restore data getters if needed
  std::span<const IndirectCommand> getIndirectCommands() const { return sceneDrawCommands; }
  std::span<const CullableInstance> getInstances() const { return allInstances; }
  std::span<const BBox> getBboxes() const { return bboxes; }

  std::span<const IndirectCommand> getSceneObjectsIndirectCommands() const
  {
    return sceneObjectsDrawCommands;
  }
  std::span<const IndirectCommand> getTerrainIndirectCommands() const
  {
    return terrainChunksDrawCommands;
  }
  std::span<const IndirectCommand> getVegetationIndirectCommands() const
  {
    return vegetationDrawCommands;
  }

  std::pair<uint32_t, uint32_t> getSceneObjectsIndirectCommandsSubrange() const
  {
    return {
      uint32_t(sceneObjectsDrawCommands.data() - sceneDrawCommands.data()),
      uint32_t(sceneObjectsDrawCommands.size())};
  }
  std::pair<uint32_t, uint32_t> getTerrainIndirectCommandsSubrange() const
  {
    return {
      uint32_t(terrainChunksDrawCommands.data() - sceneDrawCommands.data()),
      uint32_t(terrainChunksDrawCommands.size())};
  }
  std::pair<uint32_t, uint32_t> getVegetationIndirectCommandsSubrange() const
  {
    return {
      uint32_t(vegetationDrawCommands.data() - sceneDrawCommands.data()),
      uint32_t(vegetationDrawCommands.size())};
  }

  std::span<const glm::mat4> getInstanceMatrices() { return instanceMatrices; }
  std::span<const uint32_t> getInstanceMeshes() { return instanceMeshes; }
  std::span<const Mesh> getMeshes() { return meshes; }
  std::span<const RenderElement> getRenderElements() { return renderElements; }

  vk::Buffer getVertexBuffer() { return unifiedVbuf.get(); }
  vk::Buffer getIndexBuffer() { return unifiedIbuf.get(); }

  etna::VertexByteStreamFormatDescription getVertexFormatDescription();

  const UniformLights& getLights() const { return *lightsData; }

  std::span<const glm::vec2> getVegetationTemplateData() const
  {
    return vegetationTemplateBufferData;
  }

  std::span<const etna::Image> getTextures() const { return textures; }
  std::span<const etna::Sampler> getSamplers() const { return samplers; }

  const etna::Image& getTex(TexId tid) const
  {
    const auto& t = textures[size_t(tid)];

    if (size_t(tid) >= sceneTextures.size())
      return t;

    const auto& st = sceneTextures[size_t(tid)];
    ETNA_ASSERT(st.tid == tid);

    if (st.acqReady())
    {
      return t;
    }
    else
    {
      if (st.isCube)
        return cubeTexStub;
      else
        return planarTexStub;
    }
  }
  const etna::Sampler& getSmp(SmpId sid) const { return samplers[size_t(sid)]; }

  std::span<const etna::Image> getPointLightMaps() const { return pointLightMaps; }
  std::span<const etna::Image> getSpotLightMaps() const { return spotLightMaps; }
  std::span<const etna::Image> getDirectionalLightCsmCascades() const
  {
    return directionalLightCsmCascades;
  }

  const etna::Buffer& getInstanceMatricesBuf() const { return matricesBuf; }
  const etna::Buffer& getIndirectCommandsBuf() const { return indirectDrawBuf; }
  const etna::Buffer& getBboxesBuf() const { return bboxesBuf; }
  const etna::Buffer& getInstancesBuf() const { return instancesBuf; }
  const etna::Buffer& getMaterialParamsBuf() const { return materialParamsBuf; }

  bool hasTerrain() const { return terrainData.has_value(); }
  const TerrainSourceData& getTerrainData() const
  {
    ETNA_ASSERT(hasTerrain());
    return *terrainData;
  }

  bool isTerrainTexture(TexId tid) const
  {
    if (!hasTerrain())
      return false;
    if (tid == TexId::INVALID)
      return false;
    if (unpack_tex_smp_id_pair(terrainData->heightmapTexSmp).tid == tid)
      return true;
    if (unpack_tex_smp_id_pair(terrainData->splattingMaskTexSmp).tid == tid)
      return true;
    for (size_t i = 0; i < TERRAIN_MAX_DETAILS; ++i)
    {
      MaterialId mid = terrainData->details[i].matId;
      if (mid == NO_MATERIAL)
        continue;
      const Material& mat = materialParams[size_t(mid)];
      if (unpack_tex_smp_id_pair(mat.normalTexSmp).tid == tid)
        return true;
      if (unpack_tex_smp_id_pair(mat.baseColorTexSmp).tid == tid)
        return true;
      if (unpack_tex_smp_id_pair(mat.metalnessRoughnessTexSmp).tid == tid)
        return true;
      if (unpack_tex_smp_id_pair(mat.diffuseTexSmp).tid == tid)
        return true;
      // if (unpack_tex_smp_id_pair(mat.specularGlossinessTexSmp).tid == tid)
      //   return true;
      if (unpack_tex_smp_id_pair(mat.heightDisplacementTexSmp).tid == tid)
        return true;
    }
    return false;
  }

  bool hasSkybox() const { return skyboxData.has_value(); }
  const SkyboxSourceData& getSkyboxData() const
  {
    ETNA_ASSERT(hasSkybox());
    return *skyboxData;
  }

  // For imgui, kinda hacky
  UniformLights& lightsRW() { return *lightsData; }

  static constexpr std::array<std::string_view, 5> SUPPORTED_EXTENSIONS = {
    "KHR_lights_punctual",
    "KHR_materials_pbrSpecularGlossiness",
    "KHR_mesh_quantization",
    "JB_terrain",
    "JB_skybox"};

  std::vector<TexId> tickTransfer(vk::CommandBuffer cmd_buf);

private:
  std::optional<tinygltf::Model> loadModel(std::filesystem::path path);

  struct ProcessedInstances
  {
    std::vector<glm::mat4> matrices;
    std::vector<uint32_t> meshes;
    std::vector<uint32_t> lights;
  };

  ProcessedInstances processInstances(
    const tinygltf::Model& model, const SceneMultiplexing& multiplex = {}) const;

  struct Vertex
  {
    // First 3 floats are position, 4th float is a packed normal
    glm::vec4 positionAndNormal;
    // First 2 floats are tex coords, 3rd is a packed tangent, 4th is padding
    glm::vec4 texCoordAndTangentAndPadding;
  };

  static_assert(sizeof(Vertex) == sizeof(float) * 8);

  struct ProcessedMeshes
  {
    std::span<Vertex> vertices;
    std::span<uint32_t> indices;
    std::vector<RenderElement> relems;
    std::vector<Mesh> meshes;
    std::vector<IndirectCommand> sceneDrawCommands;
    std::vector<BBox> bboxes;
    std::vector<CullableInstance> allInstances;
    size_t firstTerrainCommand;
  };

  struct ProcessedLights
  {
    std::unique_ptr<UniformLights> desc;
    std::span<const etna::Image> pointLightShadowmaps;
    std::span<const etna::Image> spotLightShadowmaps;
    std::span<const etna::Image> directionalLightCsmCascadeMaps{};
  };

  ProcessedMeshes processMeshes(
    const tinygltf::Model& model, std::span<const MaterialId> material_remapping) const;

  // @TODO: restore const, somehow
  ProcessedLights processLights(
    const tinygltf::Model& model,
    std::span<glm::mat4> instances,
    std::span<uint32_t> instance_mapping,
    const SceneShadowsSetup& shadows_setup);

  void startDataUpload(
    std::span<const Vertex> vertices,
    std::span<const uint32_t> indices,
    std::span<const glm::mat4> instance_matrices,
    std::span<const IndirectCommand> draw_commands,
    std::span<const BBox> boxes,
    std::span<const CullableInstance> instances,
    std::span<const Material> material_params);

  void streamingLoop();

private:
  tinygltf::TinyGLTF loader;
  tinygltf::Model model;
  std::filesystem::path scenePath;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  etna::PerFrameTransferHelper streamer;

  // @NOTE: keeping meshes and relems around can help add live scene editing
  std::vector<RenderElement> renderElements;
  std::vector<Mesh> meshes;
  std::vector<glm::mat4> instanceMatrices;
  std::vector<uint32_t> instanceMeshes;

  std::vector<IndirectCommand> sceneDrawCommands;
  std::vector<BBox> bboxes;
  std::vector<CullableInstance> allInstances;

  std::vector<Material> materialParams;

  std::span<IndirectCommand> sceneObjectsDrawCommands;
  std::span<IndirectCommand> terrainChunksDrawCommands;
  std::span<IndirectCommand> vegetationDrawCommands;

  std::unique_ptr<UniformLights> lightsData{};

  std::optional<TerrainSourceData> terrainData{};
  std::optional<SkyboxSourceData> skyboxData{};

  // @TODO: do we support reentrability in selectScene?
  std::vector<etna::Image> textures{};
  std::vector<etna::Sampler> samplers{};
  std::span<const etna::Image> pointLightMaps{};
  std::span<const etna::Image> spotLightMaps{};
  std::span<const etna::Image> directionalLightCsmCascades{}; // @TODO: should be an mdspan

  std::vector<SceneTextureDesc> sceneTextures{};
  size_t texturesUploaded = 0;
  etna::Image planarTexStub;
  etna::Image cubeTexStub;
  etna::Sampler samplerStub;

  std::jthread streamingThread;
  std::atomic_flag sceneInited{};

  etna::Buffer unifiedVbuf;
  etna::Buffer unifiedIbuf;

  // @TODO: drag out into WR to be tweakable
  etna::Buffer matricesBuf;
  etna::Buffer bboxesBuf;
  etna::Buffer indirectDrawBuf;
  etna::Buffer instancesBuf;
  etna::Buffer materialParamsBuf;

  etna::Buffer vegetationTemplateBuffer;
  std::vector<glm::vec2> vegetationTemplateBufferData{};

  struct SceneDataUpload
  {
    etna::AsyncImageUploadState planarStubGpuUpload;
    std::array<etna::AsyncImageUploadState, 6> cubeStubGpuUpload;
    etna::AsyncBufferUploadState unifiedVbufGpuUpload;
    etna::AsyncBufferUploadState unifiedIbufGpuUpload;
    etna::AsyncBufferUploadState matricesBufGpuUpload;
    etna::AsyncBufferUploadState indirectDrawBufGpuUpload;
    etna::AsyncBufferUploadState bboxesBufGpuUpload;
    etna::AsyncBufferUploadState instancesBufGpuUpload;
    etna::AsyncBufferUploadState materialParamsBufGpuUpload;
    etna::AsyncBufferUploadState vegetationTemplateBufferGpuUpload;
    bool done = false;
  } sceneDataUpload{};
};
