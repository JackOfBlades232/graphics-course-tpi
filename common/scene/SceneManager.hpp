#pragma once

#include <cstdint>
#include <filesystem>
#include <array>
#include <string>

#include <glm/glm.hpp>
#include <tiny_gltf.h>
#include <etna/Buffer.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/BlockingTransferHelper.hpp>
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

class SceneManager
{
public:
  SceneManager();

  void selectScene(std::filesystem::path path, const SceneMultiplexing& multiplex = {});

  // @TODO: restore data getters if needed
  std::span<const IndirectCommand> getIndirectCommands() const { return sceneDrawCommands; }
  std::span<const CullableInstance> getInstances() const { return allInstances; }

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

  std::span<const glm::mat4> getInstanceMatrices() { return instanceMatrices; }
  std::span<const uint32_t> getInstanceMeshes() { return instanceMeshes; }
  std::span<const Mesh> getMeshes() { return meshes; }
  std::span<const RenderElement> getRenderElements() { return renderElements; }

  vk::Buffer getVertexBuffer() { return unifiedVbuf.get(); }
  vk::Buffer getIndexBuffer() { return unifiedIbuf.get(); }

  etna::VertexByteStreamFormatDescription getVertexFormatDescription();

  const UniformLights& getLights() const { return *lightsData; }

  std::span<const etna::Image> getTextures() const { return textures; }
  std::span<const etna::Sampler> getSamplers() const { return samplers; }

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

  using ProcessedLights = std::unique_ptr<UniformLights>;

  ProcessedMeshes processMeshes(
    const tinygltf::Model& model, std::span<const MaterialId> material_remapping) const;

  ProcessedLights processLights(
    const tinygltf::Model& model,
    std::span<glm::mat4> instances,
    std::span<uint32_t> instance_mapping) const;

  void uploadData(
    std::span<const Vertex> vertices,
    std::span<const uint32_t> indices,
    std::span<const glm::mat4> instance_matrices,
    std::span<const IndirectCommand> draw_commands,
    std::span<const BBox> boxes,
    std::span<const CullableInstance> instances,
    std::span<const Material> material_params);

private:
  tinygltf::TinyGLTF loader;
  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  etna::BlockingTransferHelper transferHelper;

  // @NOTE: keeping meshes and relems around can help add live scene editing
  std::vector<RenderElement> renderElements;
  std::vector<Mesh> meshes;
  std::vector<glm::mat4> instanceMatrices;
  std::vector<uint32_t> instanceMeshes;

  std::vector<IndirectCommand> sceneDrawCommands;
  std::vector<BBox> bboxes;
  std::vector<CullableInstance> allInstances;

  std::span<IndirectCommand> sceneObjectsDrawCommands;
  std::span<IndirectCommand> terrainChunksDrawCommands;

  std::unique_ptr<UniformLights> lightsData{};

  std::optional<TerrainSourceData> terrainData{};
  std::optional<SkyboxSourceData> skyboxData{};

  // @TODO: do we support reentrability in selectScene?
  std::vector<etna::Image> textures{};
  std::vector<etna::Sampler> samplers{};

  etna::Buffer unifiedVbuf;
  etna::Buffer unifiedIbuf;

  // @TODO: drag out into WR to be tweakable
  etna::Buffer matricesBuf;
  etna::Buffer indirectDrawBuf;
  etna::Buffer bboxesBuf;
  etna::Buffer instancesBuf;
  etna::Buffer materialParamsBuf;

  bool loaded = false;
};
