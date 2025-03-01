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

#include <quantization.h>
#include <materials.h>


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

SceneManager::ProcessedMeshes SceneManager::processMeshes(
  const tinygltf::Model& model, std::span<const MaterialId> material_remapping) const
{
  ProcessedMeshes result;

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
        .materialId =
          prim.material == -1 ? MaterialId::INVALID : material_remapping[prim.material]});
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

void SceneManager::uploadData(
  std::span<const Vertex> vertices,
  std::span<const uint32_t> indices,
  std::span<const Material> material_params)
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

  // @TODO: it isn't big, maybe make uniform?
  materialParamsBuf = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = material_params.size_bytes(),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "materialParamsBuf",
  });
  materialParamsBufSizeBytes = uint32_t(material_params.size_bytes());

  transferHelper.uploadBuffer<Vertex>(*oneShotCommands, unifiedVbuf, 0, vertices);
  transferHelper.uploadBuffer<uint32_t>(*oneShotCommands, unifiedIbuf, 0, indices);
  transferHelper.uploadBuffer<Material>(*oneShotCommands, materialParamsBuf, 0, material_params);
}

void SceneManager::selectScene(std::filesystem::path path)
{
  auto maybeModel = loadModel(path);
  if (!maybeModel.has_value())
    return;

  ETNA_ASSERT(!loaded);
  loaded = true;

  auto model = std::move(*maybeModel);

  // @TODO: only load referenced

  // @TODO: pull out
  //
  // @TODO: Maybe bake all this shit into bindata? Instances, everything. How fast it would be!
  std::vector<size_t> samplerRemapping{};
  {
    samplers.emplace_back(etna::Sampler::CreateInfo{
      .filter = vk::Filter::eNearest,
      .addressMode = vk::SamplerAddressMode::eRepeat,
      .name = "<default sampler>"});

    // @TODO: only load referenced
    auto hashGltfSampler = [](const tinygltf::Sampler& smp) {
      auto hasher = std::hash<int>{};
      return hasher(smp.minFilter) ^ (hasher(smp.wrapS) << 1);
    };

    std::vector<size_t> samplerHashes{};
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
          ? vk::SamplerAddressMode::eClampToEdge
          : (loadedSampler.wrapS == TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT
               ? vk::SamplerAddressMode::eMirroredRepeat
               : vk::SamplerAddressMode::eRepeat);

        samplers.emplace_back(etna::Sampler::CreateInfo{
          .filter = filterMode, .addressMode = addressMode, .name = loadedSampler.name});
      }
    }
  }

  std::vector<Material> materialParams{};
  std::vector<MaterialId> materialRemapping{};
  std::vector<vk::Format> requiredImageFormats{};
  requiredImageFormats.resize(model.images.size(), vk::Format::eUndefined);
  {
    // @TODO: only load referenced
    auto translateMaterial = [&model, &samplerRemapping, &requiredImageFormats](
                               const tinygltf::Material& gmat) {
      Material mat{};

      auto idPairForTexture = [&](int id) {
        if (id < 0)
          return TexSmpIdPair::INVALID;
        const auto& gtex = model.textures[id];
        const uint32_t samplerId = gtex.sampler < 0 ? 0 : samplerRemapping[gtex.sampler];
        // @TODO graceful
        ETNA_ASSERT(gtex.source >= 0 && gtex.source <= 65535);
        ETNA_ASSERT(samplerId >= 0 && samplerId <= 65535);
        return pack_tex_smp_id_pair(TexId{uint16_t(gtex.source)}, SmpId{uint16_t(samplerId)});
      };

      auto setTexFmt = [&](int id, vk::Format fmt) {
        if (id < 0)
          return;
        if (requiredImageFormats[id] == fmt)
          return;

        // @TODO: graceful
        ETNA_ASSERT(requiredImageFormats[id] == vk::Format::eUndefined);
        requiredImageFormats[id] = fmt;
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
          mat.diffuseColorFactor = 0xFFFFFFFF;

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
          mat.diffuseTexSmp = TexSmpIdPair::INVALID;

        // @TODO: spec/gloss
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
        materialRemapping.push_back(MaterialId(std::distance(it, materialParams.begin())));
      }
      else
      {
        materialRemapping.push_back(MaterialId(materialParams.size()));
        materialParams.push_back(mat);
      }
    }
  }

  {
    textures.reserve(model.images.size());
    for (size_t i = 0; i < model.images.size(); ++i)
    {
      auto& loadedImg = model.images[i];

      ETNA_ASSERT(
        loadedImg.bits == 8 && loadedImg.component == 4 &&
        loadedImg.pixel_type ==
          TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE); // @TODO: support or graciously err

      // Needed until it's pruned for usage
      auto format = requiredImageFormats[i] == vk::Format::eUndefined ? vk::Format::eR8G8B8A8Unorm
                                                                      : requiredImageFormats[i];

      etna::Image img = etna::get_context().createImage(etna::Image::CreateInfo{
        .extent = {(uint32_t)loadedImg.width, (uint32_t)loadedImg.height, 1},
        .name = loadedImg.name,
        .format = format,
        .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
          vk::ImageUsageFlagBits::eTransferDst});

      // @TODO: gen mips
      transferHelper.uploadImage(
        *oneShotCommands,
        img,
        0,
        0,
        {(const std::byte*)loadedImg.image.data(), loadedImg.image.size()});

      loadedImg.image.clear();
      loadedImg.image.shrink_to_fit();

      textures.push_back(std::move(img));
    }
  }

  auto [instMats, instMeshes, instLights] = processInstances(model);
  instanceMatrices = std::move(instMats);
  instanceMeshes = std::move(instMeshes);

  lightsData = processLights(model, instanceMatrices, instLights);

  auto [verts, inds, relems, meshs] = processMeshes(model, materialRemapping);
  renderElements = std::move(relems);
  meshes = std::move(meshs);
  uploadData(verts, inds, materialParams);
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
