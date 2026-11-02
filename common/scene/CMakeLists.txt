
add_library(scene
  SceneManager.cpp
  ViewContext.cpp
  gltf_jb_ext/JB_terrain/JbTerrain.cpp
  gltf_jb_ext/JB_skybox/JbSkybox.cpp
  gltf_jb_ext/JB_water/JbWater.cpp
  gltf_jb_ext/JB_noise/JbNoise.cpp
  gltf_jb_ext/JB_weather/JbWeather.cpp)

target_compile_definitions(tinygltf PUBLIC TINYGLTF_NO_EXTERNAL_IMAGE)

target_include_directories(scene PUBLIC .. gltf_jb_ext)

target_link_libraries(scene PUBLIC glm::glm tinygltf etna render_utils render_components)

target_add_shaders_slang(scene
  SHADER shaders/culling_shaders.slang reset_indirect_buf
  SHADER shaders/culling_shaders.slang reset_view_context
  SHADER shaders/culling_shaders.slang perform_culling
  SHADER shaders/culling_shaders.slang calculate_depth_bounds
)
