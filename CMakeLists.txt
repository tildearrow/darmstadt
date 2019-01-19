cmake_minimum_required(VERSION 3.0)

project(darmstadt LANGUAGES C)

add_executable(darmstadt main.c)

install(TARGETS darmstadt RUNTIME DESTINATION bin)
