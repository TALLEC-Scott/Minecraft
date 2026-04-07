cmake_minimum_required(VERSION 3.25)
project(minecraft)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COMMON_SOURCES
    src/main.cpp
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
    src/game_state.cpp
    src/ui_renderer.cpp
    src/menu.cpp
    src/miniaudio.cpp
)

if(EMSCRIPTEN)
  # --- Emscripten / WebAssembly build ---
  add_executable(minecraft ${COMMON_SOURCES})
  target_include_directories(minecraft PRIVATE include Libraries/include)
  target_compile_options(minecraft PRIVATE -O2)
  # Prevent system library paths from leaking into the Emscripten link
  set(CMAKE_EXE_LINKER_FLAGS "")
  set_property(DIRECTORY PROPERTY LINK_DIRECTORIES "")

  set_target_properties(minecraft PROPERTIES
    SUFFIX ".html"
    LINK_FLAGS "\
      -sUSE_GLFW=3 \
      -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
      -sFULL_ES3=1 \
      -sALLOW_MEMORY_GROWTH=1 \
      -sINITIAL_MEMORY=256MB \
      -sSTACK_SIZE=1MB \
      --preload-file ${CMAKE_SOURCE_DIR}/assets@assets \
      --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html \
    "
  )

else()
  # --- Desktop build ---

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

  add_executable(minecraft ${COMMON_SOURCES} src/glad.c)
  target_include_directories(minecraft PRIVATE include Libraries/include)
  target_compile_options(minecraft PRIVATE $<$<CONFIG:Release>:-O3 -march=native>)

  target_link_libraries(minecraft
      ${OPENGL_gl_LIBRARY}
      glfw
      Threads::Threads
      dl
  )

  # --- Unit Tests ---
  option(BUILD_TESTS "Build unit tests" ON)
  if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    include(GoogleTest)

    add_executable(tests
        tests/test_terrain.cpp
        tests/test_coordinates.cpp
        tests/test_biomes.cpp
        tests/test_settings.cpp
        tests/test_block_properties.cpp
        tests/test_block_placement.cpp
        tests/test_collision.cpp
        src/TerrainGenerator.cpp
        src/perlin_noise.cpp
        src/game_state.cpp
    )
    target_include_directories(tests PRIVATE include Libraries/include)
    target_link_libraries(tests GTest::gtest_main)
    set_target_properties(tests PROPERTIES CXX_CLANG_TIDY "")
    gtest_discover_tests(tests)
  endif()

endif()
