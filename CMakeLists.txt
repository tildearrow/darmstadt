cmake_minimum_required(VERSION 3.0)

project(darmstadt)

include_directories(/usr/include/libdrm)

add_executable(darmstadt main.cpp cache.cpp audio.cpp jack.cpp pulse.cpp log.cpp string.cpp time.cpp)
target_link_libraries(darmstadt drm pthread va va-drm avutil avcodec avformat jack pulse pulse-simple)

install(TARGETS darmstadt RUNTIME DESTINATION bin)
