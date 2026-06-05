cmake_minimum_required(VERSION 3.10)

project(simdtoml LANGUAGES C)

add_executable(simdtoml main.c)

install(TARGETS simdtoml RUNTIME DESTINATION bin)
