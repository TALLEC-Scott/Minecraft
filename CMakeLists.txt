cmake_minimum_required(VERSION 3.25)
project(minecraft)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Treat compiler warnings as errors. On by default (like the linter);
# disable with -DWARNINGS_AS_ERRORS=OFF if third-party code misbehaves.
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
if(WARNINGS_AS_ERRORS)
  if(MSVC)
    set(WARNING_FLAGS /W4 /WX)
  else()
    set(WARNING_FLAGS -Wall -Wextra -Werror)
  endif()
else()
  set(WARNING_FLAGS)
endif()

set(COMMON_SOURCES
    src/main.cpp
    src/camera.cpp
    src/chunk.cpp
    src/chunk_mesh.cpp
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
    src/WaterSimulator.cpp
    src/inventory.cpp
    src/world_save.cpp
    src/tnt_entity.cpp
    src/entity_manager.cpp
    src/entity_cube_renderer.cpp
    src/particle_system.cpp
)

if(EMSCRIPTEN)
  # --- Emscripten / WebAssembly build ---
  add_executable(minecraft ${COMMON_SOURCES})
  target_include_directories(minecraft PRIVATE include)
  target_include_directories(minecraft SYSTEM PRIVATE Libraries/include)
  target_compile_options(minecraft PRIVATE -O2 ${WARNING_FLAGS})
  # Prevent system library paths from leaking into the Emscripten link
  set(CMAKE_EXE_LINKER_FLAGS "")
  set_property(DIRECTORY PROPERTY LINK_DIRECTORIES "")

  set_target_properties(minecraft PROPERTIES
    SUFFIX ".html"
    LINK_FLAGS "\
      -sUSE_GLFW=3 \
      -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
      -sALLOW_MEMORY_GROWTH=1 \
      -sINITIAL_MEMORY=256MB \
      -sSTACK_SIZE=1MB \
      -lidbfs.js \
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

  # Optional Tracy profiler. Off by default so normal builds pay nothing.
  # Build with -DBUILD_WITH_TRACY=ON to enable; the game will listen on
  # 127.0.0.1:8086 for the Tracy viewer to connect.
  option(BUILD_WITH_TRACY "Build with the Tracy profiler client" OFF)
  if(BUILD_WITH_TRACY)
    include(FetchContent)
    FetchContent_Declare(
      tracy
      GIT_REPOSITORY https://github.com/wolfpld/tracy.git
      GIT_TAG v0.11.1
      GIT_SHALLOW TRUE
    )
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    # Older mingw-w64 Windows SDK headers don't have
    # TraceSetInformation / GetProcessIdOfThread / RelationProcessorDie,
    # so skip Tracy's system-trace path when cross-compiling.
    set(TRACY_NO_SYSTEM_TRACING ON CACHE BOOL "" FORCE)
    # WSL2's exposed CPU info doesn't advertise invariant TSC, so Tracy
    # refuses to init with its default timer. Fall back to a clock-based
    # timer that works without TSC invariance.
    set(TRACY_TIMER_FALLBACK ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tracy)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
      # Force-include a shim that handles mingw-w64 v10 vs Tracy quirks:
      # orders winsock2.h correctly and provides an inline __get_cpuid
      # since <cpuid.h> can't coexist with <intrin.h> on this toolchain.
      target_compile_options(TracyClient PRIVATE
          -include ${CMAKE_SOURCE_DIR}/cmake/tracy_mingw_prefix.h)
    endif()
  endif()

  add_executable(minecraft ${COMMON_SOURCES} src/glad.c)
  target_include_directories(minecraft PRIVATE include)
  target_include_directories(minecraft SYSTEM PRIVATE Libraries/include)
  # WARNING_FLAGS via generator expression so they apply only to C++ TUs
  # (glad.c is a third-party C file that may trip warnings under -Werror).
  target_compile_options(minecraft PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${WARNING_FLAGS}>)
  if(BUILD_WITH_TRACY)
    target_link_libraries(minecraft TracyClient)
    target_compile_definitions(minecraft PRIVATE TRACY_ENABLE=1)
  endif()

  if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # MinGW cross-compile. libstdc++.a and libgcc_eh.a (auto-appended by
    # gcc after our listed libs) reference winpthread symbols, which the
    # linker only gets one pass at. Wrap everything in --start-group /
    # --end-group so the linker does multiple resolution passes and both
    # directions resolve.
    # -Wno-stringop-overflow: mingw-w64 g++ 10 fires a false-positive when
    # moving an array of std::unique_ptr (SparseSkyLight::data); the
    # synthesized move assignment is provably safe.
    target_compile_options(minecraft PRIVATE $<$<CONFIG:Release>:-O3>
                                             $<$<COMPILE_LANGUAGE:CXX>:-Wno-stringop-overflow>)
    target_link_options(minecraft PRIVATE
        "LINKER:--start-group"
        "LINKER:--end-group")
    target_link_libraries(minecraft
        -Wl,--start-group
        ${CMAKE_SOURCE_DIR}/Libraries/lib/libglfw3_mingw.a
        opengl32
        gdi32
        psapi
        /usr/x86_64-w64-mingw32/lib/libpthread.a
        /usr/x86_64-w64-mingw32/lib/libwinpthread.a
        -lstdc++ -lgcc_eh -lwinpthread
        -Wl,--end-group
    )
  else()
    find_package(OpenGL REQUIRED)
    find_package(glfw3 3.3 REQUIRED)
    find_package(Threads REQUIRED)
    target_compile_options(minecraft PRIVATE $<$<CONFIG:Release>:-O3 -march=native>)
    target_link_libraries(minecraft
        ${OPENGL_gl_LIBRARY}
        glfw
        Threads::Threads
        dl
    )
  endif()

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
        tests/test_chunk_section.cpp
        tests/test_water.cpp
        tests/test_sparse_light.cpp
        tests/test_world_resolver.cpp
        tests/test_chunk_mesh.cpp
        tests/test_world_save.cpp
        tests/test_inventory.cpp
        tests/test_tnt.cpp
        src/TerrainGenerator.cpp
        src/perlin_noise.cpp
        src/game_state.cpp
        src/WaterSimulator.cpp
        src/miniaudio.cpp
        src/chunk_mesh.cpp
        src/tnt_entity.cpp
    )
    # tests/stubs is searched before include/ so the water-sim tests can
    # compile src/WaterSimulator.cpp against minimal stubs of chunk.h /
    # ChunkManager.h / world.h without dragging in OpenGL, threading, or
    # rendering dependencies.
    target_include_directories(tests PRIVATE tests/stubs include)
    target_include_directories(tests SYSTEM PRIVATE Libraries/include)
    target_compile_options(tests PRIVATE ${WARNING_FLAGS})
    target_link_libraries(tests GTest::gtest_main)
    set_target_properties(tests PROPERTIES CXX_CLANG_TIDY "")
    gtest_discover_tests(tests)
  endif()

endif()
