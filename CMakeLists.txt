cmake_minimum_required(VERSION 3.15)
project(simdtoml C)

# Force C11 standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Critical optimization flags for GCC and Clang.
# -O3: Maximum performance optimizations.
# -mavx2: Enables AVX2 vector instructions support (required for _mm256_...).
# -msse4.2: Enables SSE4.2 string/text processing instructions (required for _mm_cmpistri).
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -mavx2 -msse4.2 -Wall -Wextra")
endif()

# Define the core static library
add_library(simdtoml src/simdtoml.c)
target_include_directories(simdtoml PUBLIC include)

# Define the main test/benchmark executable
add_executable(main src/main.c)
target_link_libraries(main simdtoml)
