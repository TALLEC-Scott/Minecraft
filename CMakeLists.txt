cmake_minimum_required(VERSION 3.25)
project(minecraft)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)


add_executable(minecraft
         main.cpp glad.c camera.cpp chunk.cpp cube.cpp shader.cpp stb_image.cpp texture.cpp)
target_include_directories(minecraft PRIVATE include Libraries/include)

target_link_libraries(minecraft
        ${OPENGL_gl_LIBRARY}
        glfw
        )
