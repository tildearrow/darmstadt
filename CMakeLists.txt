cmake_minimum_required(VERSION 3.0)

project(darmstadt)

set(DEPENDENCIES_INCLUDE_DIRS "/usr/include/libdrm")
set(DEPENDENCIES_DEFINES "")
set(DEPENDENCIES_COMPILE_OPTIONS "")
set(DEPENDENCIES_LIBRARIES drm gbm pthread va va-drm avutil avcodec avformat pulse pulse-simple GL EGL)
set(DEPENDENCIES_LIBRARY_DIRS "")
set(DEPENDENCIES_LINK_OPTIONS "")

set(DARM_SOURCES
main.cpp
cache.cpp
audio.cpp
pulse.cpp
log.cpp
string.cpp
time.cpp)

find_package(PkgConfig REQUIRED)

pkg_check_modules(JACK jack)
if (JACK_FOUND)
  list(APPEND DARM_SOURCES jack.cpp)
  list(APPEND DEPENDENCIES_INCLUDE_DIRS ${JACK_INCLUDE_DIRS})
  list(APPEND DEPENDENCIES_DEFINES HAVE_JACK)
  list(APPEND DEPENDENCIES_COMPILE_OPTIONS ${JACK_CFLAGS_OTHER})
  list(APPEND DEPENDENCIES_LIBRARIES ${JACK_LIBRARIES})
  list(APPEND DEPENDENCIES_LIBRARY_DIRS ${JACK_LIBRARY_DIRS})
  list(APPEND DEPENDENCIES_LINK_OPTIONS ${JACK_LDFLAGS_OTHER})
endif()

add_executable(darmstadt ${DARM_SOURCES})

target_include_directories(darmstadt SYSTEM PRIVATE ${DEPENDENCIES_INCLUDE_DIRS})
target_compile_definitions(darmstadt PRIVATE ${DEPENDENCIES_DEFINES})
target_compile_options(darmstadt PRIVATE ${DEPENDENCIES_COMPILE_OPTIONS})
target_link_libraries(darmstadt PRIVATE ${DEPENDENCIES_LIBRARIES})
target_link_directories(darmstadt PRIVATE ${DEPENDENCIES_LIBRARY_DIRS})
target_link_options(darmstadt PRIVATE ${DEPENDENCIES_LINK_OPTIONS})

install(TARGETS darmstadt RUNTIME DESTINATION bin)
