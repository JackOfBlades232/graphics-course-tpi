#include <cstddef>
#include <glm/fwd.hpp>
#include <tiny_gltf.h>
#include <glm/glm.hpp>
#include <quantization.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <vector>
#include <string>
#include <filesystem>

#define LOGERR(fmt_, ...) fprintf(stderr, "[ERROR] " fmt_ "\n", ##__VA_ARGS__)
#define LOGWARN(fmt_, ...) fprintf(stderr, "[WARNING] " fmt_ "\n", ##__VA_ARGS__)

#define FAIL(fmt_, ...)                                                                            \
  do                                                                                               \
  {                                                                                                \
    LOGERR(fmt_, ##__VA_ARGS__);                                                                   \
    exit(1);                                                                                       \
  } while (0)

#define VERIFY(e_, fmt_, ...)                                                                      \
  do                                                                                               \
  {                                                                                                \
    if (!(e_))                                                                                     \
      FAIL(fmt_, ##__VA_ARGS__);                                                                   \
  } while (0)

int main(int argc, char** argv)
{
  VERIFY(argc >= 2, "Invalid number of args: specify a gltf asset to bake");
  std::filesystem::path path{argv[1]};

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
      LOGERR("glTF: %s", error.c_str());
    FAIL("glTF: Failed to load model!");
  }

  if (!warning.empty())
    LOGWARN("glTF: %s", warning.c_str());

  if (
    !model.extensions.empty() || !model.extensionsRequired.empty() || !model.extensionsUsed.empty())
  {
    LOGWARN("glTF: No glTF extensions are currently implemented!");
  }

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

  // Either vertices or quantizedVertices is used based on quantize flag
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
        LOGWARN(
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
        hasNormals ? reinterpret_cast<const uint8_t*>(model.buffers[bufViews[2]->buffer].data.data()) +
            bufViews[2]->byteOffset + accessors[2]->byteOffset
                   : nullptr,
        hasTangents ? reinterpret_cast<const uint8_t*>(model.buffers[bufViews[3]->buffer].data.data()) +
            bufViews[3]->byteOffset + accessors[3]->byteOffset
                    : nullptr,
        hasTexcoord ? reinterpret_cast<const uint8_t*>(model.buffers[bufViews[4]->buffer].data.data()) +
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
        auto &vtx = vertices.emplace_back();
        glm::vec3 normal{0};
        glm::vec3 tangent{0};

        memcpy(&vtx.pos, ptrs[1], sizeof(vtx.pos));

        if (hasNormals)
          memcpy(&normal, ptrs[2], sizeof(normal));
        if (hasTangents)
          memcpy(&tangent, ptrs[3], sizeof(tangent));
        if (hasTexcoord)
          memcpy(&vtx.texcoord, ptrs[4], sizeof(vtx.texcoord));

        vtx.norm = quantize3fnorm(normal);
        vtx.tang = quantize3fnorm(tangent);

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

      // @HUH: do norm/tang componentTypes need to be short? I oct encode and then quantize, which
      // would be the other way around. Anyways, this will not be ok for third party tools, as we
      // decode on the gpu, not in streaming.

      tinygltf::Accessor indAccessor{};
      indAccessor.bufferView = 0;
      indAccessor.byteOffset = indOffset;
      indAccessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
      indAccessor.count = indexCount;
      indAccessor.type = TINYGLTF_TYPE_SCALAR;
      indAccessor.minValues = accessors[0]->minValues;
      indAccessor.maxValues = accessors[0]->maxValues;

      tinygltf::Accessor posAccessor{};
      posAccessor.bufferView = 1;
      posAccessor.byteOffset = vertsOffset;
      posAccessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
      posAccessor.count = vertexCount;
      posAccessor.type = TINYGLTF_TYPE_VEC3;
      posAccessor.minValues = accessors[1]->minValues;
      posAccessor.maxValues = accessors[1]->maxValues;

      tinygltf::Accessor normAccessor{};
      normAccessor.bufferView = 2;
      normAccessor.byteOffset = vertsOffset;
      normAccessor.componentType = TINYGLTF_COMPONENT_TYPE_BYTE; // quantized
      normAccessor.count = vertexCount;
      normAccessor.type = TINYGLTF_TYPE_VEC3;
      normAccessor.normalized = true;

      tinygltf::Accessor tcAccessor{};
      tcAccessor.bufferView = 3;
      tcAccessor.byteOffset = vertsOffset;
      tcAccessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
      tcAccessor.count = vertexCount;
      tcAccessor.type = TINYGLTF_TYPE_VEC2;

      tinygltf::Accessor tangAccessor{};
      tangAccessor.bufferView = 4;
      tangAccessor.byteOffset = vertsOffset;
      tangAccessor.componentType = TINYGLTF_COMPONENT_TYPE_BYTE; // quantized
      tangAccessor.count = vertexCount;
      tangAccessor.type = TINYGLTF_TYPE_VEC3;
      tangAccessor.normalized = true;

      const size_t accessorBase = combinedAccessors.size();

      combinedAccessors.push_back(indAccessor);
      combinedAccessors.push_back(posAccessor);
      combinedAccessors.push_back(normAccessor);
      combinedAccessors.push_back(tcAccessor);
      combinedAccessors.push_back(tangAccessor);

      prim.attributes.clear();

      prim.indices = (int)accessorBase;
      prim.attributes["POSITION"] = (int)accessorBase + 1;
      prim.attributes["NORMAL"] = (int)accessorBase + 2;
      prim.attributes["TEXCOORD_0"] = (int)accessorBase + 3;
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

  tinygltf::BufferView indView{};
  indView.name = "indView";
  indView.buffer = 0;
  indView.byteOffset = vertsBytes;
  indView.byteLength = indBytes;
  indView.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;

  tinygltf::BufferView posView{};
  posView.name = "posView";
  posView.buffer = 0;
  posView.byteOffset = 0;
  posView.byteLength = vertsBytes;
  posView.byteStride = vertSize;
  posView.target = TINYGLTF_TARGET_ARRAY_BUFFER;

  tinygltf::BufferView tcView{};
  tcView.name = "tcView";
  tcView.buffer = 0;
  tcView.byteOffset = 16;
  tcView.byteLength = vertsBytes;
  tcView.byteStride = vertSize;
  tcView.target = TINYGLTF_TARGET_ARRAY_BUFFER;

  tinygltf::BufferView normView{};
  normView.name = "normView";
  normView.buffer = 0;
  normView.byteOffset = 12;
  normView.byteLength = vertsBytes;
  normView.byteStride = vertSize;
  normView.target = TINYGLTF_TARGET_ARRAY_BUFFER;

  tinygltf::BufferView tangView{};
  tangView.name = "tangView";
  tangView.buffer = 0;
  tangView.byteOffset = 24;
  tangView.byteLength = vertsBytes;
  tangView.byteStride = vertSize;
  tangView.target = TINYGLTF_TARGET_ARRAY_BUFFER;

  model.buffers = {std::move(combinedBuffer)};
  model.bufferViews = {indView, posView, normView, tcView, tangView};
  model.accessors = std::move(combinedAccessors);

  model.extensionsRequired.emplace_back("KHR_mesh_quantization");
  model.extensionsUsed.emplace_back("KHR_mesh_quantization");

  path.replace_extension("");
  path.replace_filename("baked/" + path.filename().string());
  path.replace_extension(".gltf");

  if (!std::filesystem::exists(path.parent_path()))
    std::filesystem::create_directory(path.parent_path());

  bool res = api.WriteGltfSceneToFile(&model, path.string(), false, false, true, false);
  VERIFY(res, "Failed to write baked scene to %s", path.string().c_str());

  return 0;
}
