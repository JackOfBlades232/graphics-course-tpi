
add_executable(hello_slang
  main.cpp
)

target_link_libraries(hello_slang PRIVATE glm::glm etna)

target_add_shaders_slang(hello_slang
  shaders/hello.slang computeMain
)
