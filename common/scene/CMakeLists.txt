
add_library(scene
  SceneManager.cpp
  gltf_jb_ext/JB_terrain/JbTerrain.cpp
  gltf_jb_ext/JB_skybox/JbSkybox.cpp)

target_include_directories(scene PUBLIC .. gltf_jb_ext)

target_link_libraries(scene PUBLIC glm::glm tinygltf etna render_utils)
