
add_executable(deferred_baker
  main.cpp
)

target_link_libraries(deferred_baker
  PRIVATE tinygltf
  PRIVATE glm::glm
  PRIVATE render_utils
  PRIVATE spdlog)
