cmake_minimum_required(VERSION 3.25)
project(minecraft)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)


add_executable(minecraft
         main.cpp glad.c camera.cpp chunk.cpp cube.cpp shader.cpp
        stb_image.cpp texture.cpp world.cpp TerrainGenerator.cpp perlin_noise.cpp ChunkManager.cpp ChunkManager.h
        texture_array.cpp)
target_include_directories(minecraft PRIVATE include Libraries/include)

target_compile_options(minecraft PRIVATE $<$<CONFIG:Release>:-O3 -march=native>)

target_link_libraries(minecraft
        ${OPENGL_gl_LIBRARY}
        glfw
        )
