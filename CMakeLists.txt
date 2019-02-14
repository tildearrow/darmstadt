cmake_minimum_required(VERSION 3.0)

project(darmstadt)

include_directories(/usr/include/libdrm)

add_executable(darmstadt main.cpp time.cpp)
target_link_libraries(darmstadt drm pthread va va-drm)

install(TARGETS darmstadt RUNTIME DESTINATION bin)
