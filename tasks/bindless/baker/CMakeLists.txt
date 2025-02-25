
add_executable(bindless_baker
  main.cpp
)

target_link_libraries(bindless_baker
  PRIVATE tinygltf
  PRIVATE glm::glm
  PRIVATE render_utils
  PRIVATE spdlog)
