
include(${PROJECT_SOURCE_DIR}/cmake/common.cmake)

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
  shaders/static_mesh_depth.vert
  shaders/terrain_mesh.vert
  shaders/terrain_mesh.tesc
  shaders/terrain_mesh.tese
  shaders/terrain_mesh.frag
  shaders/terrain_mesh_depth.tese
  shaders/grass_generate_clear_chunks.comp
  shaders/grass_generate_cull_chunks.comp
  shaders/grass_generate_sort_chunks.comp
  shaders/grass_generate_prepare_inst_command.comp
  shaders/grass_generate_instances.comp
  shaders/grass_mesh.vert
  shaders/grass_mesh.frag
  shaders/grass_mesh_no_prepass.frag
  shaders/grass_mesh_depth.vert
  shaders/grass_mesh_depth.frag
  shaders/ssao_generate.frag
  shaders/ssao_blur.frag
  shaders/water_time_indep_spectra_gen.comp
  shaders/water_time_indep_spectra_conjugate.comp
  shaders/water_time_spectra_gen.comp
  shaders/water_double_fft.comp
  shaders/water_extract_geodata.comp
)
