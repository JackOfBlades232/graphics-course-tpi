
add_executable(renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
)

target_link_libraries(renderer
  PRIVATE glfw etna glm::glm wsi gui scene render_utils render_components)

target_add_shaders(renderer
  shaders/gbuffer_resolve.frag
  shaders/clipmap_gen.comp
  shaders/reset_terrain_chunk_height_bounds.comp
  shaders/generate_terrain_chunk_height_bounds.comp
  shaders/transfer_terrain_chunk_height_bounds.comp
  shaders/transfer_light_mats.comp
  shaders/static_mesh.vert
  shaders/static_mesh.frag
  shaders/static_mesh_shadow.vert
  shaders/terrain_mesh.vert
  shaders/terrain_mesh.tesc
  shaders/terrain_mesh.tese
  shaders/terrain_mesh.frag
  shaders/terrain_mesh_shadow.tese
  shaders/grass_generate_clear_chunks.comp
  shaders/grass_generate_cull_chunks.comp
  shaders/grass_generate_instances.comp
)
