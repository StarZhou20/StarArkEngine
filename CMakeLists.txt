cmake_minimum_required(VERSION 3.20)
project(StarArk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

cmake_policy(SET CMP0135 NEW)

# --- FetchContent: all via URL zip (no git clone) ---
include(FetchContent)

FetchContent_Declare(glfw
    URL file://${CMAKE_SOURCE_DIR}/deps_cache/glfw-3.4.zip
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glm
    URL file://${CMAKE_SOURCE_DIR}/deps_cache/glm-1.0.1-light.zip
)

FetchContent_Declare(glew-cmake
    URL file://${CMAKE_SOURCE_DIR}/deps_cache/glew-cmake-2.2.0.zip
)

FetchContent_Declare(spdlog
    URL file://${CMAKE_SOURCE_DIR}/deps_cache/spdlog-1.15.0.zip
)

# Assimp: only import OBJ / FBX / glTF to keep build small
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
FetchContent_Declare(assimp
    URL file://${CMAKE_SOURCE_DIR}/deps_cache/assimp-5.4.3.zip
)

FetchContent_MakeAvailable(glfw glm glew-cmake spdlog assimp)

# glm-light extracts flat (glm-src/glm.hpp). Create a wrapper directory
# with a "glm" junction so #include <glm/glm.hpp> works.
set(_glm_wrapper "${CMAKE_BINARY_DIR}/_deps/glm-wrapper")
if(NOT EXISTS "${_glm_wrapper}/glm")
    file(MAKE_DIRECTORY "${_glm_wrapper}")
    if(WIN32)
        file(TO_NATIVE_PATH "${_glm_wrapper}/glm" _link_path)
        file(TO_NATIVE_PATH "${glm_SOURCE_DIR}" _target_path)
        execute_process(COMMAND cmd /c mklink /J "${_link_path}" "${_target_path}")
    else()
        file(CREATE_LINK "${glm_SOURCE_DIR}" "${_glm_wrapper}/glm" SYMBOLIC)
    endif()
endif()
target_include_directories(glm-header-only INTERFACE "${_glm_wrapper}")

add_subdirectory(engine)
add_subdirectory(game)
