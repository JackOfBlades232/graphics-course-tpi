
add_library(render_components
  HistogramEqTonemapper.cpp
  ReinhardTonemapper.cpp
)

target_include_directories(render_components PUBLIC ..)

# Allows C++ code to inlcude C++-GLSL compat
target_include_directories(render_components INTERFACE shaders)
# Allow GLSL code to include helper files and compat
target_shader_include_directories(render_components INTERFACE shaders)

target_link_libraries(render_components PUBLIC etna glm::glm function2::function2 utils render_utils)

target_add_shaders(render_components
  shaders/histogram_minmax.comp
  shaders/histogram_pre_refine.comp
  shaders/histogram_binning.comp
  shaders/histogram_distribution.comp
  shaders/histogram_clear.comp
  shaders/histogram_debug.frag
  shaders/histogram_tonemap.frag
  shaders/reinhard_tonemap.frag
)

