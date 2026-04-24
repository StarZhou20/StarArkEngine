file(GLOB_RECURSE ENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/debug/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rhi/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rhi/opengl/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rendering/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/*.cpp"
)

add_library(StarArkEngine STATIC ${ENGINE_SOURCES})

# Include from project root so headers use: #include "engine/platform/Window.h"
target_include_directories(StarArkEngine PUBLIC
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
)

find_package(OpenGL REQUIRED)

target_link_libraries(StarArkEngine PUBLIC
    glfw
    libglew_static
    glm::glm
    spdlog::spdlog
    assimp::assimp
    OpenGL::GL
)

if(WIN32)
    target_link_libraries(StarArkEngine PUBLIC shell32 ole32)
endif()

target_compile_definitions(StarArkEngine PUBLIC GLEW_STATIC)

# --- Phase M1: Shader hot reload -------------------------------------------
# When enabled, ShaderManager polls shader files for mtime changes every frame
# and recompiles in-place. Default: ON for Debug builds, OFF for Release.
option(ARK_SHADER_HOT_RELOAD "Enable shader hot-reload polling" ON)
if(ARK_SHADER_HOT_RELOAD)
    target_compile_definitions(StarArkEngine PUBLIC ARK_SHADER_HOT_RELOAD=1)
else()
    target_compile_definitions(StarArkEngine PUBLIC ARK_SHADER_HOT_RELOAD=0)
endif()

# Engine shader files live in engine/shaders and are deployed by each exe
# target (see game/CMakeLists.txt and samples/CMakeLists.txt).
set(ARK_ENGINE_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders" CACHE INTERNAL "")
