
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
  shaders/culling.comp
  shaders/reset_indirect_commands.comp
  shaders/clipmap_gen.comp
  shaders/static_mesh.vert
  shaders/static_mesh.frag
  shaders/terrain_mesh.vert
  shaders/terrain_mesh.tesc
  shaders/terrain_mesh.tese
  shaders/terrain_mesh.frag
  shaders/tonemap.frag
  shaders/histogram_minmax.comp
  shaders/histogram_binning.comp
  shaders/histogram_distribution.comp
  shaders/histogram_clear.comp
  shaders/histogram_debug.frag
)
