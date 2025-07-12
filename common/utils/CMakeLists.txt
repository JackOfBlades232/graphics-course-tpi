
add_library(utils Common.cpp)

target_include_directories(utils PUBLIC ..)

target_link_libraries(utils PUBLIC etna)
