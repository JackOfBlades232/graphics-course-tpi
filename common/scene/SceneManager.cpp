#define GLM_ENABLE_EXPERIMENTAL

#include "SceneManager.hpp"
#include "etna/Assert.hpp"

#include <JB_terrain/JbTerrain.hpp>
#include <JB_skybox/JbSkybox.hpp>
#include <JB_water/JbWater.hpp>

#include <render_utils/Common.hpp>
#include <render_utils/GrassUtils.hpp>

#include <quantization.h>
#include <materials.h>
#include <geometry.h>

#include <spdlog/spdlog.h>
#include <fmt/std.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/quaternion.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <stb_image.h>

#include <filesystem>
#include <stack>
#include <unordered_map>

static constexpr const std::byte STUB_COLOR[4] = {
  std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};

SceneManager::SceneManager(const etna::GpuWorkCount& wc)
  : oneShotCommands{etna::get_context().createOneShotCmdMgr()}
  , streamer{etna::PerFrameTransferHelper::CreateInfo{
      .totalStagingSize = 4096 * 4096 * 4, .wc = &wc}}
  , streamingThread{[this](std::stop_token stop) { streamingLoop(stop); }}
{
}

std::optional<tinygltf::Model> SceneManager::loadModel(std::filesystem::path path)
{
  tinygltf::Model m;

  std::string error;
  std::string warning;
  bool success = false;

  auto ext = path.extension();
  if (ext == ".gltf")
    success = loader.LoadASCIIFromFile(&m, &error, &warning, path.string());
  else if (ext == ".glb")
    success = loader.LoadBinaryFromFile(&m, &error, &warning, path.string());
  else
  {
    spdlog::error("glTF: Unknown glTF file extension: '{}'. Expected .gltf or .glb.", ext);
    return std::nullopt;
  }

  if (!success)
  {
    spdlog::error("glTF: Failed to load model!");
    if (!error.empty())
      spdlog::error("glTF: {}", error);
    return std::nullopt;
  }

  if (!warning.empty())
    spdlog::warn("glTF: {}", warning);

  if constexpr (SUPPORTED_EXTENSIONS.size() > 0)
  {
    std::string supportedExtsMsg{SUPPORTED_EXTENSIONS[0]};
    for (const auto& supp : std::span{SUPPORTED_EXTENSIONS}.subspan(1))
    {
      supportedExtsMsg += ", ";
      supportedExtsMsg += std::string{supp};
    }
    spdlog::info("glTF: supported extensions : {}", supportedExtsMsg);
  }
  else
    spdlog::info("glTF: no extensions supported");

  if (!m.extensions.empty() || !m.extensionsRequired.empty() || !m.extensionsUsed.empty())
  {
    for (const auto& [mext, _] : m.extensions)
    {
      if (
        std::find(m.extensionsUsed.begin(), m.extensionsUsed.end(), mext) == m.extensionsUsed.end())
      {
        spdlog::error(
          "glTF: inconsistent model, extension \"{}\" is used but not included in extensionsUsed",
          mext);
        return std::nullopt;
      }
    }
    for (const auto& rext : m.extensionsRequired)
    {

      if (
        std::find(SUPPORTED_EXTENSIONS.begin(), SUPPORTED_EXTENSIONS.end(), rext) ==
        SUPPORTED_EXTENSIONS.end())
      {
        spdlog::error("glTF: required extension \"{}\" is not supported", rext);
        return std::nullopt;
      }
    }
    for (const auto& uext : m.extensionsUsed)
    {
      if (
        std::find(SUPPORTED_EXTENSIONS.begin(), SUPPORTED_EXTENSIONS.end(), uext) ==
        SUPPORTED_EXTENSIONS.end())
      {
        spdlog::warn(
          "glTF: used extension \"{}\" is not supported and will not be displayed", uext);
      }
    }
  }

  return m;
}

SceneManager::ProcessedInstances SceneManager::processInstances(
  const tinygltf::Model& m, const SceneMultiplexing& multiplex) const
{
  std::vector nodeTransforms(m.nodes.size(), glm::identity<glm::mat4x4>());

  for (size_t nodeIdx = 0; nodeIdx < m.nodes.size(); ++nodeIdx)
  {
    const auto& node = m.nodes[nodeIdx];
    auto& transform = nodeTransforms[nodeIdx];

    if (!node.matrix.empty())
    {
      for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
          transform[i][j] = static_cast<float>(node.matrix[4 * i + j]);
    }
    else
    {
      if (!node.scale.empty())
        transform = scale(
          transform,
          glm::vec3(
            static_cast<float>(node.scale[0]),
            static_cast<float>(node.scale[1]),
            static_cast<float>(node.scale[2])));

      if (!node.rotation.empty())
        transform *= mat4_cast(glm::quat(
          static_cast<float>(node.rotation[3]),
          static_cast<float>(node.rotation[0]),
          static_cast<float>(node.rotation[1]),
          static_cast<float>(node.rotation[2])));

      if (!node.translation.empty())
        transform = translate(
          transform,
          glm::vec3(
            static_cast<float>(node.translation[0]),
            static_cast<float>(node.translation[1]),
            static_cast<float>(node.translation[2])));
    }
  }

  std::stack<size_t> vertices;
  for (auto vert : m.scenes[m.defaultScene].nodes)
    vertices.push(vert);

  while (!vertices.empty())
  {
    auto vert = vertices.top();
    vertices.pop();

    for (auto child : m.nodes[vert].children)
    {
      nodeTransforms[child] = nodeTransforms[vert] * nodeTransforms[child];
      vertices.push(child);
    }
  }

  ProcessedInstances result;

  size_t totalRelevantNodes = 0;
  {
    for (size_t i = 0; i < m.nodes.size(); ++i)
    {
      if (m.nodes[i].mesh >= 0 || m.nodes[i].light >= 0)
        ++totalRelevantNodes;
    }
    size_t multiplexedNodes =
      totalRelevantNodes * multiplex.dims.x * multiplex.dims.y * multiplex.dims.z;
    result.matrices.resize(multiplexedNodes);
    result.meshes.resize(multiplexedNodes);
    result.lights.resize(multiplexedNodes);
  }

  size_t did = 0;
  for (size_t i = 0; i < m.nodes.size(); ++i)
  {
    if (m.nodes[i].mesh >= 0 || m.nodes[i].light >= 0)
    {
      for (unsigned x = 0; x < multiplex.dims.x; ++x)
        for (unsigned y = 0; y < multiplex.dims.y; ++y)
          for (unsigned z = 0; z < multiplex.dims.z; ++z)
          {
            size_t dest = x * totalRelevantNodes * multiplex.dims.y * multiplex.dims.z +
              y * totalRelevantNodes * multiplex.dims.z + z * totalRelevantNodes + did;

            ETNA_ASSERT(dest < result.matrices.size());
            ETNA_ASSERT(dest < result.meshes.size());
            ETNA_ASSERT(dest < result.lights.size());

            const float xc = float(int(x) - int(multiplex.dims.x) / 2);
            const float yc = float(int(y) - int(multiplex.dims.y) / 2);
            const float zc = float(int(z) - int(multiplex.dims.z) / 2);

            glm::vec3 translation = multiplex.offsets * glm::vec3{xc, yc, zc};

            // @TODO: why does glm::translate not do this?
            result.matrices[dest] = nodeTransforms[i];
            result.matrices[dest][3][0] += translation[0];
            result.matrices[dest][3][1] += translation[1];
            result.matrices[dest][3][2] += translation[2];

            result.meshes[dest] = m.nodes[i].mesh;
            result.lights[dest] = m.nodes[i].light;
          }

      ++did;
    }
  }

  ETNA_ASSERT(did == totalRelevantNodes);

  return result;
}

namespace
{

struct RelemIdentifier
{
  uint32_t indexCount;
  uint32_t indexOffset;
  uint32_t vertexOffset;

  friend bool operator==(RelemIdentifier i1, RelemIdentifier i2) = default;
};
struct RelemData
{
  std::vector<DrawableInstance> instances;
  BBox bbox;
};

} // namespace

template <>
struct std::hash<RelemIdentifier>
{
  size_t operator()(const RelemIdentifier& rid) const noexcept
  {
    size_t h1 = std::hash<uint32_t>{}(rid.indexCount);
    size_t h2 = std::hash<uint32_t>{}(rid.indexOffset);
    size_t h3 = std::hash<uint32_t>{}(rid.vertexOffset);
    return h1 ^ ((h2 ^ (h3 << 1)) << 1);
  }
};

SceneManager::ProcessedMeshes SceneManager::processMeshes(
  const tinygltf::Model& m, std::span<const MaterialId> material_remapping) const
{
  ProcessedMeshes result;

  result.vertices = {
    (Vertex*)m.buffers[0].data.data(), m.bufferViews[0].byteLength / sizeof(Vertex)};
  result.indices = {
    (uint32_t*)(result.vertices.data() + result.vertices.size()),
    m.bufferViews[1].byteLength / sizeof(uint32_t)};

  {
    size_t totalPrimitives = 0;
    for (const auto& mesh : m.meshes)
      totalPrimitives += mesh.primitives.size();
    result.relems.reserve(totalPrimitives);
  }

  result.meshes.reserve(m.meshes.size());

  std::unordered_map<RelemIdentifier, RelemData> batchedInstances{};

  uint32_t totalInstCount = 0;

  for (size_t i = 0; i < m.meshes.size(); ++i)
  {
    const auto& mesh = m.meshes[i];
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<uint32_t>(result.relems.size()),
      .relemCount = static_cast<uint32_t>(mesh.primitives.size()),
    });

    std::vector<uint32_t> matrixIds{};

    for (size_t j = 0; j < instanceMeshes.size(); ++j)
    {
      if (instanceMeshes[j] == i)
        matrixIds.push_back(uint32_t(j));
    }

    for (const auto& prim : mesh.primitives)
    {
      if (prim.mode != TINYGLTF_MODE_TRIANGLES)
      {
        spdlog::warn(
          "Encountered a non-triangles primitive, these are not supported for now, skipping it!");
        --result.meshes.back().relemCount;
        continue;
      }

      const tinygltf::Accessor& indAccessor = m.accessors[prim.indices];
      const tinygltf::Accessor& posAccessor = m.accessors[prim.attributes.at("POSITION")];

      result.relems.push_back(RenderElement{
        .vertexOffset = static_cast<uint32_t>(posAccessor.byteOffset / sizeof(Vertex)),
        .indexOffset = static_cast<uint32_t>(indAccessor.byteOffset / sizeof(uint32_t)),
        .indexCount = static_cast<uint32_t>(indAccessor.count),
        .materialId =
          prim.material == -1 ? MaterialId::INVALID : material_remapping[prim.material]});

      const auto& relem = result.relems.back();

      const RelemIdentifier batchId{relem.indexCount, relem.indexOffset, relem.vertexOffset};
      auto [it, inserted] = batchedInstances.try_emplace(batchId);
      auto& data = it->second;

      for (size_t matrixId : matrixIds)
      {
        data.instances.push_back(DrawableInstance{
          shader_uint(matrixId), shader_uint(relem.materialId), 0, 0, FLT_MAX, -FLT_MAX, 0, 0});
      }

      totalInstCount += uint32_t(matrixIds.size());

      if (!inserted) // Only calculte bbox on first encounter of relem
        continue;

      BBox box{glm::vec4{100000.f}, glm::vec4{-100000.f}};
      for (uint32_t ind : result.indices.subspan(relem.indexOffset, relem.indexCount))
      {
        auto pos = result.vertices[relem.vertexOffset + ind].positionAndNormal;
        box.min = glm::min(box.min, pos);
        box.max = glm::max(box.max, pos);
      }

      // @TODO: handle no indices case? (what should be done?)

      box.min.w = box.max.w = 1.f;
      data.bbox = box;
    }
  }

  result.sceneDrawCommands.reserve(batchedInstances.size());
  result.bboxes.reserve(batchedInstances.size());
  result.allInstances.reserve(totalInstCount);
  for (auto&& [batch, data] : batchedInstances)
  {
    auto& cmd = result.sceneDrawCommands.emplace_back();
    cmd.indexCount = batch.indexCount;
    cmd.firstIndex = batch.indexOffset;
    cmd.vertexOffset = batch.vertexOffset;
    cmd.instanceCount = 0;
    cmd.firstInstance = shader_uint(result.allInstances.size());

    result.bboxes.push_back(data.bbox);

    auto instances = std::move(data.instances);
    for (DrawableInstance inst : instances)
    {
      result.allInstances.push_back(CullableInstance{
        inst.instId, inst.materialId, shader_uint(result.sceneDrawCommands.size() - 1), 0});
    }
  }

  // @NOTE: done here, if it is not added the span is just empty
  result.firstTerrainCommand = result.sceneDrawCommands.size();

  if (terrainData)
  {
    const uint32_t totalChunkCount =
      TERRAIN_FIRST_LEVEL_CHUNKS + (CLIPMAP_LEVEL_COUNT - 1) * TERRAIN_OTHER_LEVELS_CHUNKS;

    result.bboxes.reserve(result.bboxes.size() + totalChunkCount);
    result.allInstances.reserve(result.allInstances.size() + totalChunkCount);

    auto& cmd = result.sceneDrawCommands.emplace_back();
    cmd.indexCount = 4;
    cmd.firstIndex = 0;
    cmd.vertexOffset = 0;
    cmd.instanceCount = 0;
    cmd.firstInstance = shader_uint(result.allInstances.size());

    const size_t commandId = result.sceneDrawCommands.size() - 1;

    for (size_t i = 0; i < totalChunkCount; ++i)
    {
      result.allInstances.push_back(CullableInstance{
        shader_uint(i + commandId),
        shader_uint(MaterialId::INVALID), // @TODO set in scene
        shader_uint(commandId),
        TERRAIN_CHUNK_INSTANCE_FLAG});

      glm::vec3 chunkCoord = {};
      glm::vec3 chunkExtent = {};

      // @NOTE: filled dynamically from compute shaders
      chunkCoord.y = 0.f;
      chunkExtent.y = 0.f;

      if (i < TERRAIN_FIRST_LEVEL_CHUNKS)
      {
        chunkExtent.x = chunkExtent.z = CLIPMAP_EXTENT_STEP * 2.f / float(TERRAIN_CHUNKS_LEVEL_DIM);
        chunkCoord.x = float(i % TERRAIN_CHUNKS_LEVEL_DIM) * chunkExtent.x - CLIPMAP_EXTENT_STEP;
        chunkCoord.z = float(i / TERRAIN_CHUNKS_LEVEL_DIM) * chunkExtent.z - CLIPMAP_EXTENT_STEP;
      }
      else
      {
        const uint32_t level =
          (uint32_t(i) - TERRAIN_FIRST_LEVEL_CHUNKS) / TERRAIN_OTHER_LEVELS_CHUNKS + 1;
        const float levelMult = float(1 << level);
        chunkExtent.x = chunkExtent.z =
          levelMult * CLIPMAP_EXTENT_STEP * 2.f / float(TERRAIN_CHUNKS_LEVEL_DIM);

        const uint32_t chunkId = (i - TERRAIN_FIRST_LEVEL_CHUNKS) % TERRAIN_OTHER_LEVELS_CHUNKS;
        const float levelExtent = levelMult * CLIPMAP_EXTENT_STEP;

        // @NOTE: this only works for one-wide trim
        if (chunkId < TERRAIN_CHUNKS_LEVEL_DIM)
        {
          chunkCoord.x = float(chunkId) * chunkExtent.x - levelExtent;
          chunkCoord.z = -levelExtent;
        }
        else if (chunkId < TERRAIN_OTHER_LEVELS_CHUNKS - TERRAIN_CHUNKS_LEVEL_DIM)
        {
          const uint32_t yId = (chunkId - TERRAIN_CHUNKS_LEVEL_DIM) >> 1;
          const uint32_t xId = (chunkId - TERRAIN_CHUNKS_LEVEL_DIM) & 1;
          chunkCoord.z = chunkExtent.z * float(yId + 1) - levelExtent;
          chunkCoord.x = xId ? (-levelExtent) : (levelExtent - chunkExtent.x);
        }
        else
        {
          chunkCoord.x = float(chunkId - TERRAIN_OTHER_LEVELS_CHUNKS + TERRAIN_CHUNKS_LEVEL_DIM) *
              chunkExtent.x -
            levelExtent;
          chunkCoord.z = levelExtent - chunkExtent.z;
        }
      }

      result.bboxes.push_back(
        BBox{shader_vec4{chunkCoord, 1.f}, shader_vec4{chunkCoord + chunkExtent, 1.f}});
    }

    if (terrainData->vegetationTypeCount > 0)
    {
      result.vegetationDrawCommand.indexCount = 18;
      result.vegetationDrawCommand.firstIndex = 0;
      result.vegetationDrawCommand.vertexOffset = 0;
      result.vegetationDrawCommand.instanceCount = 0;
      result.vegetationDrawCommand.firstInstance = 0;
    }
  }

  return result;
}

// @TODO: dup light matrices for separate manipulation and instead put them
// in the common instance array to be able to pack more lights into the cbuf.
// Implement random object manipulation while at it
SceneManager::ProcessedLights SceneManager::processLights(
  const tinygltf::Model& m,
  std::span<glm::mat4> instances,
  std::span<uint32_t> instance_mapping,
  const SceneShadowsSetup& shadows_setup)
{
  auto lights = std::make_unique<UniformLights>();
  memset(lights.get(), 0, sizeof(lights));

  // @TODO: more optimal, no allocations/copies
  std::vector<PointLight> pointLights{};
  std::vector<SpotLight> spotLights{};
  std::vector<DirectionalLight> directionalLights{};

  pointLights.reserve(POINT_LIGHT_BUF_SIZE);
  spotLights.reserve(SPOT_LIGHT_BUF_SIZE);
  directionalLights.reserve(DIRECTIONAL_LIGHT_BUF_SIZE);

  bool pointOverflowed = false;
  bool spotOverflowed = false;
  bool directionalOverflowed = false;

  for (size_t instId = 0; instId < instance_mapping.size(); ++instId)
  {
    const uint32_t lightId = instance_mapping[instId];

    if (lightId == (uint32_t)(-1))
      continue;

    auto inst = glm::mat4x4(instances[instId]);

    const auto& l = m.lights[lightId];
    const glm::vec3 color = {(float)l.color[0], (float)l.color[1], (float)l.color[2]};

    // @TODO: more efficient if need be, direction calc too
    glm::vec3 translation;
    glm::vec3 direction;
    {
      glm::vec3 scale;
      glm::quat rotation;
      glm::vec3 skew;
      glm::vec4 perspective;
      glm::decompose(inst, scale, rotation, translation, skew, perspective);

      const glm::vec4 directionOffsetHom = inst * glm::vec4(0.f, 0.f, -1.f, 1.f);
      const glm::vec3 directionOffset = {
        directionOffsetHom.x / directionOffsetHom.w,
        directionOffsetHom.y / directionOffsetHom.w,
        directionOffsetHom.z / directionOffsetHom.w};

      direction = glm::normalize(directionOffset - translation);
    }

    if (l.type == "point")
    {
      if (pointLights.size() >= POINT_LIGHT_BUF_SIZE)
      {
        pointOverflowed = true;
        continue;
      }

      auto& dest = pointLights.emplace_back();
      dest.color = color;
      dest.intensity = (float)l.intensity;
      dest.range = (float)l.range;
      dest.position = translation;
    }
    else if (l.type == "spot")
    {
      if (spotLights.size() >= SPOT_LIGHT_BUF_SIZE)
      {
        spotOverflowed = true;
        continue;
      }

      auto& dest = spotLights.emplace_back();
      dest.color = color;
      dest.intensity = (float)l.intensity;
      dest.range = (float)l.range;
      dest.position = translation;
      dest.direction = direction;
      dest.innerConeAngle = (float)l.spot.innerConeAngle;
      dest.outerConeAngle = (float)l.spot.outerConeAngle;
    }
    else if (l.type == "directional")
    {
      if (directionalLights.size() >= DIRECTIONAL_LIGHT_BUF_SIZE)
      {
        directionalOverflowed = true;
        continue;
      }

      auto& dest = directionalLights.emplace_back();
      dest.color = color;
      dest.intensity = (float)l.intensity;
      dest.direction = direction;
    }
    else
    {
      spdlog::warn(
        "Encountered invalid light format {}, skipping, the gltf asset may be invalid", l.type);
    }
  }

  if (pointOverflowed)
  {
    spdlog::warn(
      "The model contained more point lights than supported (max={}), truncated to max count",
      POINT_LIGHT_BUF_SIZE);
  }
  if (spotOverflowed)
  {
    spdlog::warn(
      "The model contained more spot lights than supported (max={}), truncated to max count",
      SPOT_LIGHT_BUF_SIZE);
  }
  if (directionalOverflowed)
  {
    spdlog::warn(
      "The model contained more directional lights than supported (max={}), truncated to max "
      "count",
      DIRECTIONAL_LIGHT_BUF_SIZE);
  }

  ETNA_ASSERT(pointLights.size() <= POINT_LIGHT_BUF_SIZE);
  ETNA_ASSERT(spotLights.size() <= SPOT_LIGHT_BUF_SIZE);
  ETNA_ASSERT(directionalLights.size() <= DIRECTIONAL_LIGHT_BUF_SIZE);

  lights->pointLightsCount = (shader_uint)pointLights.size();
  memcpy(lights->pointLights, pointLights.data(), pointLights.size() * sizeof(pointLights[0]));

  lights->spotLightsCount = (shader_uint)spotLights.size();
  memcpy(lights->spotLights, spotLights.data(), spotLights.size() * sizeof(spotLights[0]));

  lights->directionalLightsCount = (shader_uint)directionalLights.size();
  memcpy(
    lights->directionalLights,
    directionalLights.data(),
    directionalLights.size() * sizeof(directionalLights[0]));

  const SmpId shadowSamplerId = SmpId(samplers.size());
  samplers.emplace_back(etna::Sampler::CreateInfo{
    .filter = vk::Filter::eLinear,
    .addressMode = vk::SamplerAddressMode::eClampToBorder,
    .name = "<shadowmap_sampler>",
    .compareEnable = true});

  auto nextShadowTexSmpId = [&, this] {
    ETNA_ASSERT(textures.size() <= 65535);
    return pack_tex_smp_id_pair(TexId{uint16_t(textures.size())}, SmpId{uint16_t(shadowSamplerId)});
  };

  std::span<const etna::Image> pointLightShadowmaps{};
  if (shadows_setup.allocatePointShadowTextures)
  {
    for (uint32_t i = 0; i < lights->pointLightsCount; ++i)
    {
      lights->pointLights[i].shadowmap = nextShadowTexSmpId();
      textures.emplace_back(create_image(etna::Image::CreateInfo{
        .extent = {POINT_SM_RESOLUTION, POINT_SM_RESOLUTION, 1},
        .name = fmt::format("pointlight_shadowmap{}", i),
        .format = vk::Format::eD16Unorm,
        .imageUsage =
          vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eDepthStencilAttachment,
        .layers = 6,
        .flags = vk::ImageCreateFlagBits::eCubeCompatible}));
    }
    pointLightShadowmaps = {textures.end() - lights->pointLightsCount, textures.end()};
  }

  std::span<const etna::Image> spotLightShadowmaps{};
  if (shadows_setup.allocateSpotShadowTextures)
  {
    for (uint32_t i = 0; i < lights->spotLightsCount; ++i)
    {
      lights->spotLights[i].shadowmap = nextShadowTexSmpId();
      textures.emplace_back(create_image(etna::Image::CreateInfo{
        .extent = {SPOT_SM_RESOLUTION, SPOT_SM_RESOLUTION, 1},
        .name = fmt::format("spotlight_shadowmap{}", i),
        .format = vk::Format::eD16Unorm,
        .imageUsage =
          vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eDepthStencilAttachment}));
    }
    spotLightShadowmaps = {textures.end() - lights->spotLightsCount, textures.end()};
  }

  std::span<const etna::Image> directionalLightCsmCascadeMaps{};
  if (shadows_setup.allocateDirectionalShadowTextures)
  {
    for (uint32_t i = 0; i < lights->directionalLightsCount; ++i)
    {
      // @TODO: this is piggy as fuck, should be a layered image. Improve the bindless system!
      for (uint32_t j = 0; j < CSM_CASCADE_COUNT; ++j)
      {
        lights->directionalLights[i].shadowmapCascades[j].map = nextShadowTexSmpId();
        textures.emplace_back(create_image(etna::Image::CreateInfo{
          .extent = {CSM_CASCADE_RESOLUTION, CSM_CASCADE_RESOLUTION, 1},
          .name = fmt::format("directional{}_csm_shadowmap[{}]", i, j),
          .format = vk::Format::eD16Unorm,
          .imageUsage =
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eDepthStencilAttachment}));
      }
    }
    // @NOTE ub
    directionalLightCsmCascadeMaps = {
      textures.end() - lights->directionalLightsCount, textures.end()};
  }

  return {
    std::move(lights), pointLightShadowmaps, spotLightShadowmaps, directionalLightCsmCascadeMaps};
}

void SceneManager::startDataUpload(
  std::span<const Vertex> vertices,
  std::span<const uint32_t> indices,
  std::span<const glm::mat4> instance_matrices,
  std::span<const IndirectCommand> draw_commands,
  std::span<const BBox> boxes,
  std::span<const CullableInstance> instances,
  std::span<const Material> material_params,
  std::span<const IndirectCommand> vegetation_draw_commands)
{
  unifiedVbuf = create_buffer(etna::Buffer::CreateInfo{
    .size = vertices.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "unifiedVbuf",
  });

  unifiedIbuf = create_buffer(etna::Buffer::CreateInfo{
    .size = indices.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "unifiedIbuf",
  });

  matricesBuf = create_buffer(etna::Buffer::CreateInfo{
    .size = instance_matrices.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "matricesBuf",
  });

  indirectDrawBuf = create_buffer(etna::Buffer::CreateInfo{
    .size = draw_commands.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc |
      vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "indirectDrawBuf",
  }),

  bboxesBuf = create_buffer(etna::Buffer::CreateInfo{
    .size = boxes.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "bboxesBuf",
  });

  instancesBuf = create_buffer(etna::Buffer::CreateInfo{
    .size = instances.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "instancesBuf",
  });

  // @TODO: it isn't big, maybe make uniform?
  materialParamsBuf = create_buffer(etna::Buffer::CreateInfo{
    .size = material_params.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "materialParamsBuf",
  });

  sceneDataUpload.unifiedVbufGpuUpload =
    streamer.initUploadBufferAsync<Vertex>(unifiedVbuf, 0, vertices);
  sceneDataUpload.unifiedIbufGpuUpload =
    streamer.initUploadBufferAsync<uint32_t>(unifiedIbuf, 0, indices);
  sceneDataUpload.matricesBufGpuUpload =
    streamer.initUploadBufferAsync<glm::mat4>(matricesBuf, 0, instance_matrices);
  sceneDataUpload.indirectDrawBufGpuUpload =
    streamer.initUploadBufferAsync<IndirectCommand>(indirectDrawBuf, 0, draw_commands);
  sceneDataUpload.bboxesBufGpuUpload = streamer.initUploadBufferAsync<BBox>(bboxesBuf, 0, boxes);
  sceneDataUpload.instancesBufGpuUpload =
    streamer.initUploadBufferAsync<CullableInstance>(instancesBuf, 0, instances);
  sceneDataUpload.materialParamsBufGpuUpload =
    streamer.initUploadBufferAsync<Material>(materialParamsBuf, 0, material_params);

  if (!vegetationTemplateBufferData.empty())
  {
    vegetationTemplateBuffer = create_buffer(etna::Buffer::CreateInfo{
      .size = std::span{vegetationTemplateBufferData}.size_bytes(),
      .bufferUsage =
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "vegetationTemplateBuffer",
    });
    vegetationIndirectDrawBuffer = create_buffer(etna::Buffer::CreateInfo{
      .size = vegetation_draw_commands.size_bytes(),
      .bufferUsage = vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "vegetationIndirectDrawBuffer",
    });
    sceneDataUpload.vegetationTemplateBufferGpuUpload = streamer.initUploadBufferAsync<glm::vec2>(
      vegetationTemplateBuffer, 0, vegetationTemplateBufferData);
    sceneDataUpload.vegetationIndirectDrawBufferGpuUpload =
      streamer.initUploadBufferAsync<IndirectCommand>(
        vegetationIndirectDrawBuffer, 0, vegetation_draw_commands);
  }
}

void SceneManager::selectScene(
  std::filesystem::path path,
  const SceneShadowsSetup& shadows_setup,
  const SceneMultiplexing& multiplex)
{
  ETNA_ASSERT(!sceneInited.test(std::memory_order_relaxed));

  auto maybeModel = loadModel(path);
  if (!maybeModel.has_value())
    return;

  model = std::move(*maybeModel);
  scenePath = path;

  // @TODO: prune unreferenced in baker

  // @TODO: pull out
  //
  // @TODO: Maybe bake all this shit into bindata? Instances, everything. How fast it would be?
  std::vector<size_t> samplerRemapping{};
  {
    samplers.emplace_back(etna::Sampler::CreateInfo{
      .filter = vk::Filter::eNearest,
      .addressMode = vk::SamplerAddressMode::eRepeat,
      .name = "<default_sampler>",
      .minLod = 0.f,
      .maxLod = VK_LOD_CLAMP_NONE});

    auto hashGltfSampler = [](const tinygltf::Sampler& smp) {
      auto hasher = std::hash<int>{};
      return hasher(smp.minFilter) ^ (hasher(smp.wrapS) << 1);
    };

    std::vector<size_t> samplerHashes{size_t(-1)}; // Fake hash for default sampler
    samplerHashes.reserve(model.samplers.size());
    samplerRemapping.reserve(model.samplers.size());
    for (const auto& loadedSampler : model.samplers)
    {
      size_t hash = hashGltfSampler(loadedSampler);
      if (auto it = std::find(samplerHashes.begin(), samplerHashes.end(), hash);
          it != samplerHashes.end())
      {
        samplerRemapping.push_back(std::distance(samplerHashes.begin(), it));
      }
      else
      {
        samplerRemapping.push_back(samplerHashes.size());
        samplerHashes.push_back(hash);

        const vk::Filter filterMode =
          (loadedSampler.minFilter == TINYGLTF_TEXTURE_FILTER_LINEAR ||
           loadedSampler.minFilter == TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR ||
           loadedSampler.minFilter == TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST)
          ? vk::Filter::eLinear
          : vk::Filter::eNearest;
        const vk::SamplerAddressMode addressMode =
          loadedSampler.wrapS == TINYGLTF_TEXTURE_WRAP_REPEAT
          ? vk::SamplerAddressMode::eRepeat
          : (loadedSampler.wrapS == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT
               ? vk::SamplerAddressMode::eMirroredRepeat
               : vk::SamplerAddressMode::eClampToEdge);

        samplers.emplace_back(etna::Sampler::CreateInfo{
          .filter = filterMode,
          .addressMode = addressMode,
          .name = loadedSampler.name,
          .minLod = 0.f,
          .maxLod = VK_LOD_CLAMP_NONE});
      }
    }
  }

  auto idPairForTexture = [&](int id) {
    if (id < 0)
      return TexSmpIdPair::INVALID;
    const auto& gtex = model.textures[id];
    const uint32_t samplerId = uint32_t(gtex.sampler < 0 ? 0 : samplerRemapping[gtex.sampler]);
    // @TODO graceful
    ETNA_ASSERT(gtex.source >= 0 && gtex.source <= 65535);
    ETNA_ASSERT(samplerId >= 0 && samplerId <= 65535);
    return pack_tex_smp_id_pair(TexId{uint16_t(gtex.source)}, SmpId{uint16_t(samplerId)});
  };

  materialParams.clear();

  std::vector<MaterialId> materialRemapping{};
  std::vector<vk::Format> requiredImageFormats{};
  requiredImageFormats.resize(model.images.size(), vk::Format::eUndefined);

  {
    auto translateMaterial = [&](const tinygltf::Material& gmat) {
      Material mat{};

      auto setTexFmt = [&](int id, vk::Format fmt) {
        if (id < 0)
          return;
        const auto& gtex = model.textures[id];
        const int gid = gtex.source;
        if (gid < 0)
          return;
        if (requiredImageFormats[gid] == fmt)
          return;

        ETNA_ASSERT(requiredImageFormats[gid] == vk::Format::eUndefined);
        requiredImageFormats[gid] = fmt;
      };

      // @TODO: texcoord params from material textures

      mat.normalTexSmp = idPairForTexture(gmat.normalTexture.index);
      setTexFmt(gmat.normalTexture.index, vk::Format::eR8G8B8A8Unorm);

      if (auto it = gmat.extensions.find("KHR_materials_pbrSpecularGlossiness");
          it != gmat.extensions.end())
      {
        mat.mat = MaterialType::DIFFUSE;
        const auto& params = it->second;

        if (params.Has("diffuseFactor"))
        {
          const auto& factor = params.Get("diffuseFactor");
          // @TODO: graceful
          ETNA_ASSERT(factor.IsNumber() || (factor.IsArray() && factor.ArrayLen() == 4));
          if (factor.IsNumber())
            mat.diffuseColorFactor = quantizefcol(float(factor.GetNumberAsDouble()));
          else
          {
            ETNA_ASSERT(
              factor.Get(0).IsNumber() && factor.Get(1).IsNumber() && factor.Get(2).IsNumber() &&
              factor.Get(3).IsNumber());

            mat.diffuseColorFactor = quantize4fcol(
              {float(factor.Get(0).GetNumberAsDouble()),
               float(factor.Get(1).GetNumberAsDouble()),
               float(factor.Get(2).GetNumberAsDouble()),
               float(factor.Get(3).GetNumberAsDouble())});
          }
        }
        else
        {
          mat.diffuseColorFactor = 0xFFFFFFFF;
        }

        if (params.Has("specularFactor"))
        {
          const auto& factor = params.Get("specularFactor");
          ETNA_ASSERT(factor.IsNumber() || (factor.IsArray() && factor.ArrayLen() == 3));
          if (factor.IsNumber())
          {
            mat.specularFactor = quantizefcol(float(factor.GetNumberAsDouble()));
          }
          else
          {
            ETNA_ASSERT(
              factor.Get(0).IsNumber() && factor.Get(1).IsNumber() && factor.Get(2).IsNumber());

            mat.specularFactor = quantize4fcol(
              {float(factor.Get(0).GetNumberAsDouble()),
               float(factor.Get(1).GetNumberAsDouble()),
               float(factor.Get(2).GetNumberAsDouble()),
               float(0.f)});
          }
        }
        else
        {
          mat.specularFactor = 0xFFFFFFFF;
        }

        if (params.Has("glossinessFactor"))
        {
          const auto& factor = params.Get("glossinessFactor");
          ETNA_ASSERT(factor.IsNumber());
          mat.glossinessFactor = float(factor.GetNumberAsDouble());
        }
        else
        {
          mat.glossinessFactor = 1.f;
        }

        if (params.Has("diffuseTexture"))
        {
          const auto& tex = params.Get("diffuseTexture");
          // @TODO: graceful
          ETNA_ASSERT(tex.IsObject() && tex.Has("index"));
          const auto& ind = tex.Get("index");
          ETNA_ASSERT(ind.IsInt());
          const int id = ind.GetNumberAsInt();
          mat.diffuseTexSmp = idPairForTexture(id);
          setTexFmt(id, vk::Format::eR8G8B8A8Srgb);
        }
        else
        {
          mat.diffuseTexSmp = TexSmpIdPair::INVALID;
        }

        if (params.Has("specularGlossinessTexture"))
        {
          const auto& tex = params.Get("specularGlossinessTexture");
          ETNA_ASSERT(tex.IsObject() && tex.Has("index"));
          const auto& ind = tex.Get("index");
          ETNA_ASSERT(ind.IsInt());
          const int id = ind.GetNumberAsInt();
          mat.specularGlossinessTexSmp = idPairForTexture(id);
          setTexFmt(id, vk::Format::eR8G8B8A8Srgb);
        }
        else
        {
          mat.specularGlossinessTexSmp = TexSmpIdPair::INVALID;
        }
      }
      else
      {
        mat.mat = MaterialType::PBR;

        mat.baseColorFactor = quantize4fcol(
          {float(gmat.pbrMetallicRoughness.baseColorFactor[0]),
           float(gmat.pbrMetallicRoughness.baseColorFactor[1]),
           float(gmat.pbrMetallicRoughness.baseColorFactor[2]),
           float(gmat.pbrMetallicRoughness.baseColorFactor[3])});
        mat.baseColorTexSmp = idPairForTexture(gmat.pbrMetallicRoughness.baseColorTexture.index);
        mat.metalnessFactor = float(gmat.pbrMetallicRoughness.metallicFactor);
        mat.roughnessFactor = float(gmat.pbrMetallicRoughness.roughnessFactor);
        mat.metalnessRoughnessTexSmp =
          idPairForTexture(gmat.pbrMetallicRoughness.metallicRoughnessTexture.index);

        setTexFmt(gmat.pbrMetallicRoughness.baseColorTexture.index, vk::Format::eR8G8B8A8Srgb);
        setTexFmt(
          gmat.pbrMetallicRoughness.metallicRoughnessTexture.index, vk::Format::eR8G8B8A8Unorm);
      }

      if (auto it = gmat.extensions.find("KHR_materials_diffuse_transmission");
          it != gmat.extensions.end())
      {
        const auto& params = it->second;

        if (params.Has("diffuseTransmissionFactor"))
        {
          const auto& factor = params.Get("diffuseTransmissionFactor");
          ETNA_ASSERT(factor.IsNumber());
          mat.diffuseTransmissionFactor = float(factor.GetNumberAsDouble());
        }
        else
        {
          mat.diffuseTransmissionFactor = 0.f;
        }

        if (params.Has("diffuseTransmissionColorFactor"))
        {
          const auto& factor = params.Get("diffuseTransmissionColorFactor");
          ETNA_ASSERT(factor.IsNumber() || (factor.IsArray() && factor.ArrayLen() == 3));
          if (factor.IsNumber())
          {
            mat.diffuseTransmissionColorFactor = quantizefcol(float(factor.GetNumberAsDouble()));
          }
          else
          {
            ETNA_ASSERT(
              factor.Get(0).IsNumber() && factor.Get(1).IsNumber() && factor.Get(2).IsNumber());

            mat.diffuseTransmissionColorFactor = quantize4fcol(
              {float(factor.Get(0).GetNumberAsDouble()),
               float(factor.Get(1).GetNumberAsDouble()),
               float(factor.Get(2).GetNumberAsDouble()),
               float(0.f)});
          }
        }
        else
        {
          mat.diffuseTransmissionColorFactor = 0xFFFFFFFF;
        }

        if (params.Has("diffuseTransmissionTexture"))
        {
          const auto& tex = params.Get("diffuseTransmissionTexture");
          ETNA_ASSERT(tex.IsObject() && tex.Has("index"));
          const auto& ind = tex.Get("index");
          ETNA_ASSERT(ind.IsInt());
          const int id = ind.GetNumberAsInt();
          mat.diffuseTransmissionTexSmp = idPairForTexture(id);
          setTexFmt(id, vk::Format::eR8G8B8A8Unorm);
        }
        else
        {
          mat.diffuseTransmissionTexSmp = TexSmpIdPair::INVALID;
        }

        if (params.Has("diffuseTransmissionColorTexture"))
        {
          const auto& tex = params.Get("diffuseTransmissionColorTexture");
          ETNA_ASSERT(tex.IsObject() && tex.Has("index"));
          const auto& ind = tex.Get("index");
          ETNA_ASSERT(ind.IsInt());
          const int id = ind.GetNumberAsInt();
          mat.diffuseTransmissionColorTexSmp = idPairForTexture(id);
          setTexFmt(id, vk::Format::eR8G8B8A8Srgb);
        }
        else
        {
          mat.diffuseTransmissionColorTexSmp = TexSmpIdPair::INVALID;
        }
      }
      else
      {
        mat.diffuseTransmissionFactor = 0.f;
        mat.diffuseTransmissionColorFactor = 0xFFFFFFFF;
        mat.diffuseTransmissionTexSmp = NO_TEXTURE_ID;
        mat.diffuseTransmissionColorTexSmp = NO_TEXTURE_ID;
      }

      if (auto jbExtMaybe = jb_terrain_parse_material_desc(gmat))
      {
        mat.heightDisplacementTexSmp = idPairForTexture(jbExtMaybe->displacement);
        mat.displacementCoeff = jbExtMaybe->displacementCoeff;
      }

      return mat;
    };

    // @TODO: more efficient dedup
    materialRemapping.reserve(model.materials.size());
    for (const auto& loadedMat : model.materials)
    {
      auto mat = translateMaterial(loadedMat);
      if (auto it = std::find_if(
            materialParams.begin(),
            materialParams.end(),
            [&mat](const Material& m) { return memcmp(&m, &mat, sizeof(m)) == 0; });
          it != materialParams.end())
      {
        materialRemapping.push_back(MaterialId(std::distance(materialParams.begin(), it)));
      }
      else
      {
        materialRemapping.push_back(MaterialId(materialParams.size()));
        materialParams.push_back(mat);
      }
    }
  }

  int cubemapLoadedId = -1;

  if (auto skyboxExt = jb_skybox_parse_desc(model))
  {
    auto& data = skyboxData.emplace();
    cubemapLoadedId = skyboxExt->cubemap;
    data.cubemapTexSmp = idPairForTexture(cubemapLoadedId);
  }

  {
    textures.reserve(model.images.size());
    for (size_t i = 0; i < model.images.size(); ++i)
    {
      auto& loadedImg = model.images[i];

      // @TODO: adaptive somehow for embedded images?
      ETNA_ASSERT(loadedImg.image.empty());
      ETNA_ASSERT(!loadedImg.uri.empty());

      // Needed until it's pruned for usage
      const auto format = requiredImageFormats[i] == vk::Format::eUndefined
        ? vk::Format::eR8G8B8A8Unorm
        : requiredImageFormats[i];

      const bool isCube =
        skyboxData && cubemapLoadedId >= 0 && model.textures[cubemapLoadedId].source == int(i);

      auto& st = sceneTextures.emplace_back();
      st.tid = TexId(i);
      st.uri = loadedImg.uri;
      st.format = format;
      st.isCube = isCube;

      textures.emplace_back();
    }

    {
      planarTexStub = create_image(etna::Image::CreateInfo{
        .extent = {1, 1, 1},
        .name = "<planar tex stub>",
        .format = vk::Format::eR8G8B8A8Unorm,
        .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
          vk::ImageUsageFlagBits::eTransferDst,
        .mipLevels = 1});
      cubeTexStub = create_image(etna::Image::CreateInfo{
        .extent = {1, 1, 1},
        .name = "<cube tex stub>",
        .format = vk::Format::eR8G8B8A8Unorm,
        .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
          vk::ImageUsageFlagBits::eTransferDst,
        .layers = 6,
        .mipLevels = 1,
        .flags = vk::ImageCreateFlagBits::eCubeCompatible});

      samplerStub = etna::Sampler{etna::Sampler::CreateInfo{
        .filter = vk::Filter::eNearest,
        .addressMode = vk::SamplerAddressMode::eRepeat,
        .name = "<stub_sampler>",
        .minLod = 0.f,
        .maxLod = 0.f}};

      sceneDataUpload.planarStubGpuUpload =
        streamer.initUploadImageAsync(planarTexStub, 0, 0, STUB_COLOR);
      for (int j = 0; j < 6; ++j)
        sceneDataUpload.cubeStubGpuUpload[j] =
          streamer.initUploadImageAsync(cubeTexStub, 0, j, STUB_COLOR);
    }
  }

  if (auto waterExt = jb_water_parse_desc(model))
  {
    auto& data = waterData.emplace();
    data.waterLevel = waterExt->waterLevel;
  }

  // @TODO: make terrain also use a material?
  if (auto terrainExt = jb_terrain_parse_desc(model))
  {
    auto& data = terrainData.emplace();
    data.heightmapTexSmp = idPairForTexture(terrainExt->heightmap);
    data.splattingMaskTexSmp = idPairForTexture(terrainExt->splattingMask);
    data.noiseSeed = terrainExt->noiseSeed;
    data.rangeMin = terrainExt->rangeMin;
    data.rangeMax = terrainExt->rangeMax;

    ETNA_ASSERT(terrainExt->details.size() <= TERRAIN_MAX_DETAILS);
    ETNA_ASSERT(terrainExt->vegetations.size() <= TERRAIN_MAX_VEGETATION_TYPES);
    data.detailCount = uint32_t(terrainExt->details.size());
    data.vegetationTypeCount = uint32_t(terrainExt->vegetations.size());

    if (terrainExt->continent)
    {
      data.continentType = uint32_t(terrainExt->continent->type);
      data.continentCircleCenter = terrainExt->continent->circle.center;
      data.continentCircleInnerOuterRad =
        glm::vec2(terrainExt->continent->circle.innerRad, terrainExt->continent->circle.outerRad);
      data.continentOceanBottom = terrainExt->continent->oceanBottom;
    }
    else
    {
      data.continentType = 0;
    }

    // @TODO: do I need to make sure it's one material here as well?
    int vid = 0;
    for (const auto& veg : terrainExt->vegetations)
    {
      auto& dst = data.vegetationTypes[vid++];
      dst.height = veg.height;
      dst.radius = veg.radius;
      dst.sparsenessRadius = veg.sparsenessRadius;
      dst.matId = veg.material == -1 ? MaterialId::INVALID : materialRemapping[veg.material];
      auto chunkTemplate =
        generate_grass_chunk_template(dst.sparsenessRadius, VEGETATION_CHUNK_SIZE, 100);
      dst.templateBufferOffset = shader_uint(vegetationTemplateBufferData.size());
      dst.templateBufferSize = shader_uint(chunkTemplate.planarPositions.size());
      std::copy_n(
        chunkTemplate.planarPositions.begin(),
        chunkTemplate.planarPositions.size(),
        std::back_inserter(vegetationTemplateBufferData));
    }

    int did = 0;
    std::optional<MaterialType> detailMat{};
    for (const auto& det : terrainExt->details)
    {
      auto& dst = data.details[did++];

      dst.uvScale = det.uvScale;
      dst.heightRange = det.relHeightRange;
      dst.splattingCompId = shader_uint(det.splattingCompId);
      dst.matId = det.material == -1 ? MaterialId::INVALID : materialRemapping[det.material];
      dst.vegetationId = det.vegetation == -1 ? uint32_t(-1) : det.vegetation;
      dst.flags = (det.useSplattingMask ? TERRAIN_DETAIL_USE_MASK_FLAG : 0) |
        (det.useRelHeightRange ? TERRAIN_DETAIL_USE_RH_RANGE_FLAG : 0);

      if (MaterialId(dst.matId) != MaterialId::INVALID)
      {
        if (detailMat)
          ETNA_ASSERT(*detailMat == materialParams[size_t(dst.matId)].mat);
        else
          detailMat.emplace(materialParams[size_t(dst.matId)].mat);
      }
    }
  }

  auto [instMats, instMeshes, instLights] = processInstances(model, multiplex);
  instanceMatrices = std::move(instMats);
  instanceMeshes = std::move(instMeshes);

  auto [ld, plm, slm, dlcsm] = processLights(model, instanceMatrices, instLights, shadows_setup);
  lightsData = std::move(ld);
  pointLightMaps = plm;
  spotLightMaps = slm;
  directionalLightCsmCascades = dlcsm;

  auto
    [verts, inds, relems, meshs, commands, bboxs, insts, firstTerrainCommand, vegetationCommand] =
      processMeshes(model, materialRemapping);
  renderElements = std::move(relems);
  meshes = std::move(meshs);
  sceneDrawCommands = std::move(commands);
  bboxes = std::move(bboxs);
  allInstances = std::move(insts);

  sceneObjectsDrawCommands = std::span{sceneDrawCommands}.first(firstTerrainCommand);
  if (terrainData)
  {
    terrainChunksDrawCommands = std::span{sceneDrawCommands}.subspan(firstTerrainCommand, 1);
    if (terrainData->vegetationTypeCount > 0)
      vegetationDrawCommand = vegetationCommand;
  }

  startDataUpload(
    verts,
    inds,
    instanceMatrices,
    sceneDrawCommands,
    bboxes,
    allInstances,
    materialParams,
    std::span{&vegetationDrawCommand, 1});

  for (auto& st : sceneTextures)
  {
    std::construct_at(&st.uploadStage(), SceneTextureUploadStage::INIT);
    st.inited = true;
  }

  sceneInited.test_and_set(std::memory_order_release);
}

etna::VertexByteStreamFormatDescription SceneManager::getVertexFormatDescription()
{
  return etna::VertexByteStreamFormatDescription{
    .stride = sizeof(Vertex),
    .attributes = {
      etna::VertexByteStreamFormatDescription::Attribute{
        .format = vk::Format::eR32G32B32A32Sfloat,
        .offset = 0,
      },
      etna::VertexByteStreamFormatDescription::Attribute{
        .format = vk::Format::eR32G32B32A32Sfloat,
        .offset = sizeof(glm::vec4),
      }}};
}

std::vector<TexId> SceneManager::tickTransfer(vk::CommandBuffer cmd_buf)
{
  if (sceneFullyReady())
    return {};

  std::vector<TexId> readyTids{};

  if (auto frame = streamer.beginFrame())
  {
    if (auto upload = frame.beginUpload())
    {
      if (!sceneDataUpload.done)
      {
        sceneDataUpload.done = upload.progressImageUploadAsync(
                                 cmd_buf, sceneDataUpload.planarStubGpuUpload) &&
          std::all_of(sceneDataUpload.cubeStubGpuUpload.begin(),
                      sceneDataUpload.cubeStubGpuUpload.end(),
                      [&](auto& side) { return upload.progressImageUploadAsync(cmd_buf, side); }) &&
          upload.progressBufferUploadAsync(cmd_buf, sceneDataUpload.unifiedVbufGpuUpload) &&
          upload.progressBufferUploadAsync(cmd_buf, sceneDataUpload.unifiedIbufGpuUpload) &&
          upload.progressBufferUploadAsync(cmd_buf, sceneDataUpload.matricesBufGpuUpload) &&
          upload.progressBufferUploadAsync(cmd_buf, sceneDataUpload.indirectDrawBufGpuUpload) &&
          upload.progressBufferUploadAsync(cmd_buf, sceneDataUpload.bboxesBufGpuUpload) &&
          upload.progressBufferUploadAsync(cmd_buf, sceneDataUpload.instancesBufGpuUpload) &&
          upload.progressBufferUploadAsync(cmd_buf, sceneDataUpload.materialParamsBufGpuUpload);
        if (!vegetationTemplateBufferData.empty())
        {
          sceneDataUpload.done &= upload.progressBufferUploadAsync(
            cmd_buf, sceneDataUpload.vegetationTemplateBufferGpuUpload);
          sceneDataUpload.done &= upload.progressBufferUploadAsync(
            cmd_buf, sceneDataUpload.vegetationIndirectDrawBufferGpuUpload);
        }
        if (sceneDataUpload.done)
        {
          emit_barriers(
            cmd_buf,
            {vk::BufferMemoryBarrier2{
               .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
               .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
               .dstStageMask = vk::PipelineStageFlagBits2::eVertexInput,
               .dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead,
               .buffer = unifiedVbuf.get(),
               .size = model.bufferViews[0].byteLength},
             vk::BufferMemoryBarrier2{
               .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
               .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
               .dstStageMask = vk::PipelineStageFlagBits2::eVertexInput,
               .dstAccessMask = vk::AccessFlagBits2::eIndexRead,
               .buffer = unifiedIbuf.get(),
               .size = model.bufferViews[1].byteLength},
             vk::BufferMemoryBarrier2{
               .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
               .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
               .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
               .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
               .buffer = matricesBuf.get(),
               .size = getInstanceMatrices().size_bytes()},
             vk::BufferMemoryBarrier2{
               .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
               .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
               .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
               .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
               .buffer = indirectDrawBuf.get(),
               .size = getIndirectCommands().size_bytes()},
             vk::BufferMemoryBarrier2{
               .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
               .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
               .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
               .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
               .buffer = bboxesBuf.get(),
               .size = getBboxes().size_bytes()},
             vk::BufferMemoryBarrier2{
               .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
               .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
               .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
               .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
               .buffer = instancesBuf.get(),
               .size = getInstances().size_bytes()},
             vk::BufferMemoryBarrier2{
               .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
               .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
               .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader |
                 vk::PipelineStageFlagBits2::eFragmentShader,
               .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
               .buffer = materialParamsBuf.get(),
               .size = std::span{materialParams}.size_bytes()}});
          if (!vegetationTemplateBufferData.empty())
          {
            emit_barriers(
              cmd_buf,
              {vk::BufferMemoryBarrier2{
                .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
                .buffer = vegetationTemplateBuffer.get(),
                .size = std::span{vegetationTemplateBufferData}.size_bytes()}});
            emit_barriers(
              cmd_buf,
              {vk::BufferMemoryBarrier2{
                .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead |
                  vk::AccessFlagBits2::eShaderStorageWrite,
                .buffer = vegetationIndirectDrawBuffer.get(),
                .size = sizeof(IndirectCommand)}});
          }
          model = {};
        }
      }

      if (!upload.hasSpaceThisFrame())
        return readyTids;

      for (auto& st : sceneTextures)
      {
        etna::Image& img = textures[size_t(st.tid)];

        auto curStage = st.uploadStage().load(std::memory_order_acquire);
        if (curStage == SceneTextureUploadStage::DONE_LOADING_FROM_DISK)
        {
          uint32_t w = st.isCube ? st.as.cube.side : st.as.planar.w;
          uint32_t h = st.isCube ? st.as.cube.side : st.as.planar.h;

          img = create_image(etna::Image::CreateInfo{
            .extent = {w, h, 1},
            .name = st.uri,
            .format = st.format,
            .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
              vk::ImageUsageFlagBits::eTransferDst,
            .layers = st.isCube ? 6u : 1u,
            .mipLevels = mip_count_for_dims(w, h),
            .flags =
              st.isCube ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags{}});

          if (st.isCube)
          {
            for (int j = 0; j < 6; ++j)
            {
              st.as.cube.gpuUploadState[j] = streamer.initUploadImageAsync(
                img,
                0,
                j,
                {(const std::byte*)st.as.cube.content[j].data(), st.as.cube.content[j].size()});
            }
          }
          else
          {
            st.as.planar.gpuUploadState = streamer.initUploadImageAsync(
              img,
              0,
              0,
              {(const std::byte*)st.as.planar.content.data(), st.as.planar.content.size()});
          }

          st.uploadStage().store(
            SceneTextureUploadStage::UPLOADING_TO_GPU, std::memory_order_release);
        }
        else if (curStage != SceneTextureUploadStage::UPLOADING_TO_GPU)
        {
          continue;
        }

        if (!upload.hasSpaceThisFrame())
          break;

        bool finished = false;
        if (st.isCube)
        {
          int doneFaces = 0;
          for (auto& us : st.as.cube.gpuUploadState)
          {
            if (us.done())
            {
              ++doneFaces;
            }
            else
            {
              if (upload.progressImageUploadAsync(cmd_buf, us))
              {
                ++doneFaces;
              }
            }
          }
          if (doneFaces == 6)
          {
            gen_mips(cmd_buf, img);
            finished = true;
          }
        }
        else
        {
          if (upload.progressImageUploadAsync(cmd_buf, st.as.planar.gpuUploadState))
          {
            gen_mips(cmd_buf, img);
            finished = true;
          }
        }

        if (finished)
        {
          ++texturesUploaded;
          st.uploadStage().store(SceneTextureUploadStage::DONE, std::memory_order_release);
          st.cleanup();
          readyTids.push_back(st.tid);
        }
      }
    }
  }

  return readyTids;
}

// @TODO: cancellation on premature shutdown
void SceneManager::streamingLoop(std::stop_token stop)
{
  while (!sceneInited.test(std::memory_order_acquire))
  {
    if (stop.stop_requested())
      return;

    // @NOTE: not atomic wait! I want to be able to check cancellation token, and atomic.wait does
    // not expose a timeout from the underlying futex/WaitOnAddress.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (stop.stop_requested())
    return;

  auto sceneRoot = scenePath.parent_path();

  for (auto& st : sceneTextures)
  {
    st.uploadStage().store(SceneTextureUploadStage::LOADING_FROM_DISK, std::memory_order_release);

    auto texPath = std::filesystem::path{st.uri};
    auto realPath = sceneRoot;
    realPath.append(texPath.string());

    if (realPath.extension() != ".png")
      ETNA_PANIC("Invalid texture \"{}\", only allowed .png files", texPath);

    if (stop.stop_requested())
      return;

    int texW, texH, texChannels;
    unsigned char* texData =
      stbi_load(to_char_str(realPath.string()).c_str(), &texW, &texH, &texChannels, 4);
    ETNA_VERIFY(texData);
    ETNA_VERIFY(texChannels == 4);

    if (stop.stop_requested())
      return;

    if (st.isCube)
    {
      ETNA_ASSERT(texW % 4 == 0);
      const uint32_t side = uint32_t(texW) / 4;
      ETNA_ASSERT(uint32_t(texH) == 3 * side);

      std::array<std::vector<unsigned char>, 6> imageDatas{};
      std::array bases{
        glm::uvec2{2 * side, side},
        glm::uvec2{0, side},
        glm::uvec2{side, 0},
        glm::uvec2{side, 2 * side},
        glm::uvec2{side, side},
        glm::uvec2{3 * side, side}};

      // @TODO: faster, by line, good to measure first
      for (size_t j = 0; j < 6; ++j)
      {
        if (stop.stop_requested())
          return;

        auto& data = imageDatas[j];
        const auto& base = bases[j];

        data.resize(side * side * 4);
        size_t dstId = 0;
        for (uint32_t y = base.y; y < base.y + side; ++y)
          for (uint32_t x = base.x; x < base.x + side; ++x)
          {
            memcpy(data.data() + dstId, texData + (y * uint32_t(texW) + x) * 4, 4);
            dstId += 4;
          }
      }

      st.as.cube.content = std::move(imageDatas);
      st.as.cube.side = side;
    }
    else
    {
      // @SPEED this is also dumbbb
      std::vector<unsigned char> imageData{};
      imageData.resize(texH * texW * 4);

      memcpy(imageData.data(), texData, imageData.size());

      st.as.planar.w = uint32_t(texW);
      st.as.planar.h = uint32_t(texH);
      st.as.planar.content = std::move(imageData);
    }

    if (stop.stop_requested())
      return;

    st.uploadStage().store(
      SceneTextureUploadStage::DONE_LOADING_FROM_DISK, std::memory_order_release);

    // @SPEED: can be avoided for planar, and for cube with offline repack
    stbi_image_free(texData);

    if (stop.stop_requested())
      return;
  }
}
