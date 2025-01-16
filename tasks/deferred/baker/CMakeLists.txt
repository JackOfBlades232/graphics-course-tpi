
add_executable(deferred_baker
  main.cpp
)

target_link_libraries(deferred_baker
  PRIVATE tinygltf
  PRIVATE glm::glm
  PRIVATE deferred_common_code
  PRIVATE spdlog)
