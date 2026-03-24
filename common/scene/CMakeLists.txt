
add_library(scene
  SceneManager.cpp
  ViewContext.cpp
  gltf_jb_ext/JB_terrain/JbTerrain.cpp
  gltf_jb_ext/JB_skybox/JbSkybox.cpp)

target_compile_definitions(tinygltf PUBLIC TINYGLTF_NO_EXTERNAL_IMAGE)

target_include_directories(scene PUBLIC .. gltf_jb_ext)

target_link_libraries(scene PUBLIC glm::glm tinygltf etna render_utils render_components)

target_add_shaders(scene
  shaders/culling.comp
  shaders/reset_view_context.comp
  shaders/reset_indirect_buf.comp
  shaders/calculate_depth_bounds.comp
)
