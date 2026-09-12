
add_executable(shadowmap_slang
  main.cpp
  Renderer.cpp
  WorldRenderer.cpp
  App.cpp
)

target_link_libraries(shadowmap_slang
  PRIVATE glfw etna glm::glm wsi gui scene render_utils)

target_add_shaders_slang(shadowmap_slang
  SHADER shaders/simple.slang vertexMain fragmentMain
  SHADER shaders/simple.slang vertexMain
)
