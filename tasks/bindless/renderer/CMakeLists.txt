
add_executable(bindless_renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
)

target_link_libraries(bindless_renderer
  PRIVATE glfw etna glm::glm wsi gui scene render_utils)

target_add_shaders(bindless_renderer
  shaders/gbuffer_resolve.frag
  shaders/static_mesh.frag
  shaders/static_mesh.vert
)
