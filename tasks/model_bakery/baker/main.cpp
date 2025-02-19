#define GLM_ENABLE_EXPERIMENTAL
#include <algorithm>
#include <tiny_gltf.h>
#include <glm/fwd.hpp>
#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <spdlog/spdlog.h>
#include <render_utils/shaders/quantization.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>

#include <vector>
#include <string>
#include <filesystem>
#include <execution>

#define FAIL(fmt_, ...)                                                                            \
  do                                                                                               \
  {                                                                                                \
    spdlog::error(fmt_ __VA_OPT__(, ) __VA_ARGS__);                                                \
    exit(1);                                                                                       \
  } while (0)

#define VERIFY(e_, fmt_, ...)                                                                      \
  do                                                                                               \
  {                                                                                                \
    if (!(e_))                                                                                     \
      FAIL(fmt_ __VA_OPT__(, ) __VA_ARGS__);                                                       \
  } while (0)

uint32_t best_fit_quantize_normal(glm::vec3 normal)
{
  // @TODO: tune parameters once there is lighted data
  constexpr float LENGTH_STEP = 0.1f;
  constexpr float LENGTH_BASE = 0.1f;
  constexpr size_t STEPS_COUNT = 64; 

  std::array<float, STEPS_COUNT> errors{};
  std::fill(errors.begin(), errors.end(), 0.f);

  auto quantizeScaled = [&](size_t step_id) {
    float coeff = LENGTH_BASE + step_id * LENGTH_STEP;
    return quantize4fnorm(glm::vec4{normal * coeff, 0.f});
  };

  std::for_each(std::execution::par, errors.begin(), errors.end(), [&](float& out) {
    auto id = &out - errors.data();
    glm::vec3 dequantizedNorm = dequantize3fnorm(quantizeScaled(id));
    errors[id] = glm::angle(glm::normalize(normal), glm::normalize(dequantizedNorm));
  });

  auto it = std::min_element(errors.begin(), errors.end());
  return quantizeScaled(std::distance(errors.begin(), it));
}

int main(int argc, char** argv)
{
  VERIFY(argc >= 2, "Invalid number of args: specify a gltf asset to bake");
  std::filesystem::path path{argv[1]};

  bool bestFitNormal = false;

  for (int i = 2; i < argc; ++i)
  {
    if (strcmp(argv[i], "-bfn") == 0)
      bestFitNormal = true;
    else
      FAIL("Invalid arg: {}", argv[i]);
  }

  // Load the model

  tinygltf::TinyGLTF api;
  tinygltf::Model model;

  std::string error;
  std::string warning;
  bool success = false;

  if (path.extension() == ".gltf")
    success = api.LoadASCIIFromFile(&model, &error, &warning, path.string());
  else if (path.extension() == ".glb")
    success = api.LoadBinaryFromFile(&model, &error, &warning, path.string());
  else
    FAIL("glTF: Unknown glTF file extension, expected .gltf or .glb.");

  if (!success)
  {
    if (!error.empty())
      spdlog::error("glTF: {}", error.c_str());
    FAIL("glTF: Failed to load model!");
  }

  if (!warning.empty())
    spdlog::warn("glTF: {}", warning.c_str());

  // Now, scrape vertices and form the unified vbuf

  struct Vertex
  {
    glm::vec3 pos{};
    int32_t norm = 0;
    glm::vec2 texcoord{};
    int32_t tang = 0;
    int32_t pad_ = 0;
  };
  using Index = uint32_t;

  static_assert(sizeof(Vertex) == 32);
  static_assert(sizeof(Index) == 4);

  std::vector<Vertex> vertices{};
  std::vector<Index> indices{};

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

    vertices.reserve(vertexBytes / sizeof(Vertex));
    indices.reserve(indexBytes / sizeof(Index));
  }

  auto byteSize = []<class Container>(const Container& cont) -> size_t {
    return sizeof(cont[0]) * cont.size();
  };

  // Pre-known buffer views : 0 -- indices, 1 -- positions,
  //                          2 -- oct normals, 3 -- tc-s,
  //                          4 -- oct tangents
  std::vector<tinygltf::Accessor> combinedAccessors{};

  for (auto& mesh : model.meshes)
  {
    for (auto& prim : mesh.primitives)
    {
      if (prim.mode != TINYGLTF_MODE_TRIANGLES)
      {
        spdlog::warn(
          "Encountered a non-triangles primitive, these are not supported for now, skipping it!");
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

      const size_t vertexCount = accessors[1]->count;
      const size_t vertsOffset = byteSize(vertices);

      std::array ptrs{
        reinterpret_cast<const uint8_t*>(model.buffers[bufViews[0]->buffer].data.data()) +
          bufViews[0]->byteOffset + accessors[0]->byteOffset,
        reinterpret_cast<const uint8_t*>(model.buffers[bufViews[1]->buffer].data.data()) +
          bufViews[1]->byteOffset + accessors[1]->byteOffset,
        hasNormals
          ? reinterpret_cast<const uint8_t*>(model.buffers[bufViews[2]->buffer].data.data()) +
            bufViews[2]->byteOffset + accessors[2]->byteOffset
          : nullptr,
        hasTangents
          ? reinterpret_cast<const uint8_t*>(model.buffers[bufViews[3]->buffer].data.data()) +
            bufViews[3]->byteOffset + accessors[3]->byteOffset
          : nullptr,
        hasTexcoord
          ? reinterpret_cast<const uint8_t*>(model.buffers[bufViews[4]->buffer].data.data()) +
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
        auto& vtx = vertices.emplace_back();
        glm::vec3 normal{0};
        glm::vec4 tangent{0};

        memcpy(&vtx.pos, ptrs[1], sizeof(vtx.pos));

        if (hasNormals)
          memcpy(&normal, ptrs[2], sizeof(normal));
        if (hasTangents)
          memcpy(&tangent, ptrs[3], sizeof(tangent));
        if (hasTexcoord)
          memcpy(&vtx.texcoord, ptrs[4], sizeof(vtx.texcoord));

        if (bestFitNormal)
          vtx.norm = best_fit_quantize_normal(normal);
        else
          vtx.norm = quantize4fnorm(glm::vec4{normal, 0.f});

        vtx.tang = quantize4fnorm(tangent);

        ptrs[1] += strides[1];
        if (hasNormals)
          ptrs[2] += strides[2];
        if (hasTangents)
          ptrs[3] += strides[3];
        if (hasTexcoord)
          ptrs[4] += strides[4];
      }

      VERIFY(bufViews[0]->byteStride == 0, "Indices must not have a stride");
      const size_t indexCount = accessors[0]->count;
      const size_t indOffset = byteSize(indices);

      if (accessors[0]->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
      {
        for (size_t i = 0; i < indexCount; ++i)
        {
          uint16_t index;
          memcpy(&index, ptrs[0], sizeof(index));
          indices.push_back(index);
          ptrs[0] += 2;
        }
      }
      else if (accessors[0]->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
      {
        const size_t lastTotalIndices = indices.size();
        indices.resize(lastTotalIndices + indexCount);
        memcpy(indices.data() + lastTotalIndices, ptrs[0], sizeof(indices[0]) * indexCount);
      }

      tinygltf::Accessor indAccessor{};
      indAccessor.bufferView = 1;
      indAccessor.byteOffset = indOffset;
      indAccessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
      indAccessor.count = indexCount;
      indAccessor.type = TINYGLTF_TYPE_SCALAR;
      indAccessor.minValues = accessors[0]->minValues;
      indAccessor.maxValues = accessors[0]->maxValues;

      tinygltf::Accessor posAccessor{};
      posAccessor.bufferView = 0;
      posAccessor.byteOffset = vertsOffset;
      posAccessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
      posAccessor.count = vertexCount;
      posAccessor.type = TINYGLTF_TYPE_VEC3;
      posAccessor.minValues = accessors[1]->minValues;
      posAccessor.maxValues = accessors[1]->maxValues;

      tinygltf::Accessor normAccessor{};
      normAccessor.bufferView = 0;
      normAccessor.byteOffset = vertsOffset + 12;
      normAccessor.componentType = TINYGLTF_COMPONENT_TYPE_BYTE; // quantized
      normAccessor.count = vertexCount;
      normAccessor.type = TINYGLTF_TYPE_VEC3;
      normAccessor.normalized = true;

      tinygltf::Accessor tcAccessor{};
      tcAccessor.bufferView = 0;
      tcAccessor.byteOffset = vertsOffset + 16;
      tcAccessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
      tcAccessor.count = vertexCount;
      tcAccessor.type = TINYGLTF_TYPE_VEC2;

      tinygltf::Accessor tangAccessor{};
      tangAccessor.bufferView = 0;
      tangAccessor.byteOffset = vertsOffset + 24;
      tangAccessor.componentType = TINYGLTF_COMPONENT_TYPE_BYTE; // quantized
      tangAccessor.count = vertexCount;
      tangAccessor.type = TINYGLTF_TYPE_VEC4;
      tangAccessor.normalized = true;

      const size_t accessorBase = combinedAccessors.size();

      combinedAccessors.push_back(indAccessor);
      combinedAccessors.push_back(posAccessor);
      if (hasNormals)
        combinedAccessors.push_back(normAccessor);
      if (hasTexcoord)
        combinedAccessors.push_back(tcAccessor);
      if (hasTangents)
        combinedAccessors.push_back(tangAccessor);

      prim.attributes.clear();

      prim.indices = (int)accessorBase;
      prim.attributes["POSITION"] = (int)accessorBase + 1;
      if (hasNormals)
        prim.attributes["NORMAL"] = (int)accessorBase + 2;
      if (hasTexcoord)
        prim.attributes["TEXCOORD_0"] = (int)accessorBase + 3;
      if (hasTangents)
        prim.attributes["TANGENT"] = (int)accessorBase + 4;
    }
  }

  // We preserve the nodes/transforms, changing the buffers, bufferViews,
  // accessors and accessor indices in mesh primitives

  const size_t vertsBytes = byteSize(vertices);
  const size_t vertSize = sizeof(Vertex);
  const size_t indBytes = byteSize(indices);

  tinygltf::Buffer combinedBuffer{};
  combinedBuffer.data.resize(vertsBytes + indBytes);
  memcpy(combinedBuffer.data.data(), vertices.data(), vertsBytes);
  memcpy(combinedBuffer.data.data() + vertsBytes, indices.data(), indBytes);

  tinygltf::BufferView vertView{};
  vertView.name = "vertView";
  vertView.buffer = 0;
  vertView.byteOffset = 0;
  vertView.byteLength = vertsBytes;
  vertView.byteStride = vertSize;
  vertView.target = TINYGLTF_TARGET_ARRAY_BUFFER;

  tinygltf::BufferView indView{};
  indView.name = "indView";
  indView.buffer = 0;
  indView.byteOffset = vertsBytes;
  indView.byteLength = indBytes;
  indView.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;

  model.buffers = {std::move(combinedBuffer)};
  model.bufferViews = {vertView, indView};
  model.accessors = std::move(combinedAccessors);

  model.extensionsRequired.emplace_back("KHR_mesh_quantization");
  model.extensionsUsed.emplace_back("KHR_mesh_quantization");

  path.replace_extension("");
  path.replace_filename("baked/" + path.filename().string());
  path.replace_extension(".gltf");

  if (!std::filesystem::exists(path.parent_path()))
    std::filesystem::create_directory(path.parent_path());

  bool res = api.WriteGltfSceneToFile(&model, path.string(), false, false, true, false);
  VERIFY(res, "Failed to write baked scene to {}", path.string().c_str());

  return 0;
}
