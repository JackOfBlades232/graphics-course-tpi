#define GLM_ENABLE_EXPERIMENTAL

#include "SceneManager.hpp"
#include "etna/Assert.hpp"

#include <filesystem>
#include <stack>

#include <spdlog/spdlog.h>
#include <fmt/std.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/quaternion.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/OneShotCmdMgr.hpp>


SceneManager::SceneManager()
  : oneShotCommands{etna::get_context().createOneShotCmdMgr()}
  , transferHelper{etna::BlockingTransferHelper::CreateInfo{.stagingSize = 4096 * 4096 * 4}}
{
}

std::optional<tinygltf::Model> SceneManager::loadModel(std::filesystem::path path)
{
  tinygltf::Model model;

  std::string error;
  std::string warning;
  bool success = false;

  auto ext = path.extension();
  if (ext == ".gltf")
    success = loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
  else if (ext == ".glb")
    success = loader.LoadBinaryFromFile(&model, &error, &warning, path.string());
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

  if (
    !model.extensions.empty() || !model.extensionsRequired.empty() || !model.extensionsUsed.empty())
    spdlog::warn("glTF: No glTF extensions are currently implemented!");

  return model;
}

SceneManager::ProcessedInstances SceneManager::processInstances(const tinygltf::Model& model) const
{
  std::vector nodeTransforms(model.nodes.size(), glm::identity<glm::mat4x4>());

  for (size_t nodeIdx = 0; nodeIdx < model.nodes.size(); ++nodeIdx)
  {
    const auto& node = model.nodes[nodeIdx];
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
  for (auto vert : model.scenes[model.defaultScene].nodes)
    vertices.push(vert);

  while (!vertices.empty())
  {
    auto vert = vertices.top();
    vertices.pop();

    for (auto child : model.nodes[vert].children)
    {
      nodeTransforms[child] = nodeTransforms[vert] * nodeTransforms[child];
      vertices.push(child);
    }
  }

  ProcessedInstances result;

  // Don't overallocate matrices, they are pretty chonky.
  {
    size_t totalRelevantNodes = 0;
    for (size_t i = 0; i < model.nodes.size(); ++i)
    {
      if (model.nodes[i].mesh >= 0 || model.nodes[i].light >= 0)
        ++totalRelevantNodes;
    }
    result.matrices.reserve(totalRelevantNodes);
    result.meshes.reserve(totalRelevantNodes);
    result.lights.reserve(totalRelevantNodes);
  }

  for (size_t i = 0; i < model.nodes.size(); ++i)
  {
    if (model.nodes[i].mesh >= 0 || model.nodes[i].light >= 0)
    {
      result.matrices.push_back(nodeTransforms[i]);
      result.meshes.push_back(model.nodes[i].mesh);
      result.lights.push_back(model.nodes[i].light);
    }
  }

  return result;
}

static uint32_t encode_normal(glm::vec3 normal)
{
  const int32_t x = static_cast<int32_t>(normal.x * 32767.0f);
  const int32_t y = static_cast<int32_t>(normal.y * 32767.0f);

  const uint32_t sign = normal.z >= 0 ? 0 : 1;
  const uint32_t sx = static_cast<uint32_t>(x & 0xfffe) | sign;
  const uint32_t sy = static_cast<uint32_t>(y & 0xffff) << 16;

  return sx | sy;
}

SceneManager::ProcessedMeshes<false> SceneManager::processMeshes(const tinygltf::Model& model) const
{
  // NOTE: glTF assets can have pretty wonky data layouts which are not appropriate
  // for real-time rendering, so we have to press the data first. In serious engines
  // this is mitigated by storing assets on the disc in an engine-specific format that
  // is appropriate for GPU upload right after reading from disc.

  ProcessedMeshes<false> result;

  // Pre-allocate enough memory so as not to hit the
  // allocator on the memcpy hotpath
  {
    size_t vertexBytes = 0;
    size_t indexBytes = 0;
    for (const auto& bufView : model.bufferViews)
    {
      switch (bufView.target)
      {
      case TINYGLTF_TARGET_ARRAY_BUFFER:
        vertexBytes += bufView.byteLength;
        break;
      case TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER:
        indexBytes += bufView.byteLength;
        break;
      default:
        break;
      }
    }
    result.vertices.reserve(vertexBytes / sizeof(Vertex));
    result.indices.reserve(indexBytes / sizeof(uint32_t));
  }

  {
    size_t totalPrimitives = 0;
    for (const auto& mesh : model.meshes)
      totalPrimitives += mesh.primitives.size();
    result.relems.reserve(totalPrimitives);
  }

  result.meshes.reserve(model.meshes.size());

  for (const auto& mesh : model.meshes)
  {
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<uint32_t>(result.relems.size()),
      .relemCount = static_cast<uint32_t>(mesh.primitives.size()),
    });

    for (const auto& prim : mesh.primitives)
    {
      if (prim.mode != TINYGLTF_MODE_TRIANGLES)
      {
        spdlog::warn(
          "Encountered a non-triangles primitive, these are not supported for now, skipping it!");
        --result.meshes.back().relemCount;
        continue;
      }

      const auto normalIt = prim.attributes.find("NORMAL");
      const auto tangentIt = prim.attributes.find("TANGENT");
      const auto texcoordIt = prim.attributes.find("TEXCOORD_0");

      const bool hasNormals = normalIt != prim.attributes.end();
      const bool hasTangents = tangentIt != prim.attributes.end();
      const bool hasTexcoord = texcoordIt != prim.attributes.end();
      std::array accessorIndices{
        prim.indices,
        prim.attributes.at("POSITION"),
        hasNormals ? normalIt->second : -1,
        hasTangents ? tangentIt->second : -1,
        hasTexcoord ? texcoordIt->second : -1,
      };

      std::array accessors{
        &model.accessors[prim.indices],
        &model.accessors[accessorIndices[1]],
        hasNormals ? &model.accessors[accessorIndices[2]] : nullptr,
        hasTangents ? &model.accessors[accessorIndices[3]] : nullptr,
        hasTexcoord ? &model.accessors[accessorIndices[4]] : nullptr,
      };

      std::array bufViews{
        &model.bufferViews[accessors[0]->bufferView],
        &model.bufferViews[accessors[1]->bufferView],
        hasNormals ? &model.bufferViews[accessors[2]->bufferView] : nullptr,
        hasTangents ? &model.bufferViews[accessors[3]->bufferView] : nullptr,
        hasTexcoord ? &model.bufferViews[accessors[4]->bufferView] : nullptr,
      };

      result.relems.push_back(RenderElement{
        .vertexOffset = static_cast<uint32_t>(result.vertices.size()),
        .indexOffset = static_cast<uint32_t>(result.indices.size()),
        .indexCount = static_cast<uint32_t>(accessors[0]->count),
      });

      const size_t vertexCount = accessors[1]->count;

      std::array ptrs{
        reinterpret_cast<const std::byte*>(model.buffers[bufViews[0]->buffer].data.data()) +
          bufViews[0]->byteOffset + accessors[0]->byteOffset,
        reinterpret_cast<const std::byte*>(model.buffers[bufViews[1]->buffer].data.data()) +
          bufViews[1]->byteOffset + accessors[1]->byteOffset,
        hasNormals
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[2]->buffer].data.data()) +
            bufViews[2]->byteOffset + accessors[2]->byteOffset
          : nullptr,
        hasTangents
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[3]->buffer].data.data()) +
            bufViews[3]->byteOffset + accessors[3]->byteOffset
          : nullptr,
        hasTexcoord
          ? reinterpret_cast<const std::byte*>(model.buffers[bufViews[4]->buffer].data.data()) +
            bufViews[4]->byteOffset + accessors[4]->byteOffset
          : nullptr,
      };

      std::array strides{
        bufViews[0]->byteStride != 0
          ? bufViews[0]->byteStride
          : tinygltf::GetComponentSizeInBytes(accessors[0]->componentType) *
            tinygltf::GetNumComponentsInType(accessors[0]->type),
        bufViews[1]->byteStride != 0
          ? bufViews[1]->byteStride
          : tinygltf::GetComponentSizeInBytes(accessors[1]->componentType) *
            tinygltf::GetNumComponentsInType(accessors[1]->type),
        hasNormals ? (bufViews[2]->byteStride != 0
                        ? bufViews[2]->byteStride
                        : tinygltf::GetComponentSizeInBytes(accessors[2]->componentType) *
                          tinygltf::GetNumComponentsInType(accessors[2]->type))
                   : 0,
        hasTangents ? (bufViews[3]->byteStride != 0
                         ? bufViews[3]->byteStride
                         : tinygltf::GetComponentSizeInBytes(accessors[3]->componentType) *
                           tinygltf::GetNumComponentsInType(accessors[3]->type))
                    : 0,
        hasTexcoord ? (bufViews[4]->byteStride != 0
                         ? bufViews[4]->byteStride
                         : tinygltf::GetComponentSizeInBytes(accessors[4]->componentType) *
                           tinygltf::GetNumComponentsInType(accessors[4]->type))
                    : 0,
      };

      for (size_t i = 0; i < vertexCount; ++i)
      {
        auto& vtx = result.vertices.emplace_back();
        glm::vec3 pos;
        // Fall back to 0 in case we don't have something.
        // NOTE: if tangents are not available, one could use http://mikktspace.com/
        // NOTE: if normals are not available, reconstructing them is possible but will look ugly
        glm::vec3 normal{0};
        glm::vec3 tangent{0};
        glm::vec2 texcoord{0};
        memcpy(&pos, ptrs[1], sizeof(pos));

        // NOTE: it's faster to do a template here with specializations for all combinations than to
        // do ifs at runtime. Also, SIMD should be used. Try implementing this!
        if (hasNormals)
          memcpy(&normal, ptrs[2], sizeof(normal));
        if (hasTangents)
          memcpy(&tangent, ptrs[3], sizeof(tangent));
        if (hasTexcoord)
          memcpy(&texcoord, ptrs[4], sizeof(texcoord));


        vtx.positionAndNormal = glm::vec4(pos, std::bit_cast<float>(encode_normal(normal)));
        vtx.texCoordAndTangentAndPadding =
          glm::vec4(texcoord, std::bit_cast<float>(encode_normal(tangent)), 0);

        ptrs[1] += strides[1];
        if (hasNormals)
          ptrs[2] += strides[2];
        if (hasTangents)
          ptrs[3] += strides[3];
        if (hasTexcoord)
          ptrs[4] += strides[4];
      }

      // Indices are guaranteed to have no stride
      ETNA_VERIFY(bufViews[0]->byteStride == 0);
      const size_t indexCount = accessors[0]->count;
      if (accessors[0]->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
      {
        for (size_t i = 0; i < indexCount; ++i)
        {
          uint16_t index;
          memcpy(&index, ptrs[0], sizeof(index));
          result.indices.push_back(index);
          ptrs[0] += 2;
        }
      }
      else if (accessors[0]->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
      {
        const size_t lastTotalIndices = result.indices.size();
        result.indices.resize(lastTotalIndices + indexCount);
        memcpy(
          result.indices.data() + lastTotalIndices,
          ptrs[0],
          sizeof(result.indices[0]) * indexCount);
      }
    }
  }

  return result;
}

SceneManager::ProcessedMeshes<true> SceneManager::processBakedMeshes(
  const tinygltf::Model& model) const
{
  ProcessedMeshes<true> result;

  {
    size_t totalPrimitives = 0;
    for (const auto& mesh : model.meshes)
      totalPrimitives += mesh.primitives.size();
    result.relems.reserve(totalPrimitives);
  }

  result.meshes.reserve(model.meshes.size());

  for (const auto& mesh : model.meshes)
  {
    result.meshes.push_back(Mesh{
      .firstRelem = static_cast<uint32_t>(result.relems.size()),
      .relemCount = static_cast<uint32_t>(mesh.primitives.size()),
    });

    for (const auto& prim : mesh.primitives)
    {
      if (prim.mode != TINYGLTF_MODE_TRIANGLES)
      {
        spdlog::warn(
          "Encountered a non-triangles primitive, these are not supported for now, skipping it!");
        --result.meshes.back().relemCount;
        continue;
      }

      const tinygltf::Accessor& indAccessor = model.accessors[prim.indices];
      const tinygltf::Accessor& posAccessor = model.accessors[prim.attributes.at("POSITION")];

      result.relems.push_back(RenderElement{
        .vertexOffset = static_cast<uint32_t>(posAccessor.byteOffset / sizeof(Vertex)),
        .indexOffset = static_cast<uint32_t>(indAccessor.byteOffset / sizeof(uint32_t)),
        .indexCount = static_cast<uint32_t>(indAccessor.count),
      });
    }
  }

  result.vertices = {
    (Vertex*)model.buffers[0].data.data(), model.bufferViews[0].byteLength / sizeof(Vertex)};
  result.indices = {
    (uint32_t*)(result.vertices.data() + result.vertices.size()),
    model.bufferViews[1].byteLength / sizeof(uint32_t)};

  return result;
}

// @TODO: do lights differently once we have instancing : matrices are already
// stored in a global array. Instead of extracting it from the array on load,
// store an index to a matrix for the light in the light itself. However,
// this requires instancing so that these matrices are even on the gpu in bulk.
SceneManager::ProcessedLights SceneManager::processLights(
  const tinygltf::Model& model,
  std::span<glm::mat4x4> instances,
  std::span<uint32_t> instance_mapping) const
{
  ProcessedLights lights = std::make_unique<UniformLights>();
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

    const glm::mat4x4& inst = instances[instId];

    const auto& l = model.lights[lightId];
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
      "The model contained more directional lights than supported (max={}), truncated to max count",
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

  return lights;
}

void SceneManager::uploadData(std::span<const Vertex> vertices, std::span<const uint32_t> indices)
{
  unifiedVbuf = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = vertices.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "unifiedVbuf",
  });

  unifiedIbuf = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = indices.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "unifiedIbuf",
  });

  transferHelper.uploadBuffer<Vertex>(*oneShotCommands, unifiedVbuf, 0, vertices);
  transferHelper.uploadBuffer<uint32_t>(*oneShotCommands, unifiedIbuf, 0, indices);
}

void SceneManager::selectScene(std::filesystem::path path, SceneAssetType scene_type)
{
  auto maybeModel = loadModel(path);
  if (!maybeModel.has_value())
    return;

  ETNA_ASSERT(scene_type != SceneAssetType::NOT_LOADED);
  ETNA_ASSERT(selectedSceneType == SceneAssetType::NOT_LOADED || selectedSceneType == scene_type);
  selectedSceneType = scene_type;

  auto model = std::move(*maybeModel);

  auto [instMats, instMeshes, instLights] = processInstances(model);
  instanceMatrices = std::move(instMats);
  instanceMeshes = std::move(instMeshes);

  lightsData = processLights(model, instanceMatrices, instLights);

  switch (scene_type)
  {
  case SceneAssetType::GENERIC: {
    auto [verts, inds, relems, meshs] = processMeshes(model);
    renderElements = std::move(relems);
    meshes = std::move(meshs);
    uploadData(verts, inds);
  }
  break;
  case SceneAssetType::BAKED: {
    auto [verts, inds, relems, meshs] = processBakedMeshes(model);
    renderElements = std::move(relems);
    meshes = std::move(meshs);
    uploadData(verts, inds);
  }
  break;
  default:
    ETNA_PANIC("we have gone insane");
  }
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
