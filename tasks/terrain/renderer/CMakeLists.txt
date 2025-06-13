
add_executable(renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
)

target_link_libraries(renderer
  PRIVATE glfw etna glm::glm wsi gui scene render_utils)

target_add_shaders(renderer
  shaders/gbuffer_resolve.frag
  shaders/static_mesh.frag
  shaders/static_mesh.vert
  shaders/culling.comp
  shaders/reset_indirect_commands.comp
  shaders/clipmap_gen.comp
)
