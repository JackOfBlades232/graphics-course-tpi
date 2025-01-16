# @TODO: _actually_ move to common code
add_library(deferred_common_code INTERFACE)

target_include_directories(deferred_common_code INTERFACE .)
target_shader_include_directories(deferred_common_code INTERFACE .)

target_link_libraries(deferred_common_code INTERFACE glm::glm)
