
add_executable(baker
  main.cpp
)

target_link_libraries(baker
  PRIVATE tinygltf
  PRIVATE glm::glm
  PRIVATE render_utils
  PRIVATE spdlog)
