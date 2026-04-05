cmake_minimum_required(VERSION 3.25)
project(minecraft)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Run clang-tidy on every build (disable with -DENABLE_CLANG_TIDY=OFF)
option(ENABLE_CLANG_TIDY "Run clang-tidy during compilation" ON)
if(ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE clang-tidy)
  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXE})
    message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
  else()
    message(STATUS "clang-tidy not found, skipping")
  endif()
endif()

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(Threads REQUIRED)

add_executable(minecraft
    src/main.cpp
    src/glad.c
    src/camera.cpp
    src/chunk.cpp
    src/cube.cpp
    src/shader.cpp
    src/stb_image.cpp
    src/texture.cpp
    src/world.cpp
    src/TerrainGenerator.cpp
    src/perlin_noise.cpp
    src/ChunkManager.cpp
    src/texture_array.cpp
    src/player.cpp
)

target_include_directories(minecraft PRIVATE include Libraries/include)
target_compile_options(minecraft PRIVATE $<$<CONFIG:Release>:-O3 -march=native>)

target_link_libraries(minecraft
    ${OPENGL_gl_LIBRARY}
    glfw
    Threads::Threads
)
