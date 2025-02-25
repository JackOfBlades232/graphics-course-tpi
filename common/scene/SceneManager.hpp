#pragma once

#include <cstdint>
#include <filesystem>

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


// A single render element (relem) corresponds to a single draw call
// of a certain pipeline with specific bindings (including material data)
struct RenderElement
{
  uint32_t vertexOffset;
  uint32_t indexOffset;
  uint32_t indexCount;
  MaterialId materialId = MaterialId::INVALID;
};

// A mesh is a collection of relems. A scene may have the same mesh
// located in several different places, so a scene consists of **instances**,
// not meshes.
struct Mesh
{
  uint32_t firstRelem;
  uint32_t relemCount;
};

// @TODO: refactor baked/non baked choice

class SceneManager
{
public:
  SceneManager();

  void selectScene(std::filesystem::path path);

  // Every instance is a mesh drawn with a certain transform
  // NOTE: maybe you can pass some additional data through unused matrix entries?
  std::span<const glm::mat4x4> getInstanceMatrices() { return instanceMatrices; }
  std::span<const uint32_t> getInstanceMeshes() { return instanceMeshes; }

  // Every mesh is a collection of relems
  std::span<const Mesh> getMeshes() { return meshes; }

  // Every relem is a single draw call
  std::span<const RenderElement> getRenderElements() { return renderElements; }

  vk::Buffer getVertexBuffer() { return unifiedVbuf.get(); }
  vk::Buffer getIndexBuffer() { return unifiedIbuf.get(); }

  etna::VertexByteStreamFormatDescription getVertexFormatDescription();

  const UniformLights& getLights() const { return *lightsData; }

  std::span<const etna::Image> getTextures() const { return textures; }
  std::span<const etna::Sampler> getSamplers() const { return samplers; }
  const etna::Buffer& getMaterialParamsBuf() const { return materialParamsBuf; }

  uint32_t getMaterialParamsBufSizeBytes() const { return materialParamsBufSizeBytes; }

  // For imgui, kinda hacky
  UniformLights& lightsRW() { return *lightsData; }

private:
  std::optional<tinygltf::Model> loadModel(std::filesystem::path path);

  struct ProcessedInstances
  {
    std::vector<glm::mat4x4> matrices;
    std::vector<uint32_t> meshes;
    std::vector<uint32_t> lights;
  };

  ProcessedInstances processInstances(const tinygltf::Model& model) const;

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
  };

  using ProcessedLights = std::unique_ptr<UniformLights>;

  ProcessedMeshes processMeshes(
    const tinygltf::Model& model, std::span<const MaterialId> material_remapping) const;

  ProcessedLights processLights(
    const tinygltf::Model& model,
    std::span<glm::mat4x4> instances,
    std::span<uint32_t> instance_mapping) const;

  void uploadData(
    std::span<const Vertex> vertices,
    std::span<const uint32_t> indices,
    std::span<const Material> material_params);

private:
  tinygltf::TinyGLTF loader;
  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  etna::BlockingTransferHelper transferHelper;

  std::vector<RenderElement> renderElements;
  std::vector<Mesh> meshes;
  std::vector<glm::mat4x4> instanceMatrices;
  std::vector<uint32_t> instanceMeshes;

  etna::Buffer unifiedVbuf;
  etna::Buffer unifiedIbuf;

  // @TODO: do we support reentrability in selectScene?
  std::vector<etna::Image> textures{};
  std::vector<etna::Sampler> samplers{};

  etna::Buffer materialParamsBuf;
  uint32_t materialParamsBufSizeBytes = 0;

  std::unique_ptr<UniformLights> lightsData{};

  bool loaded = false;
};
