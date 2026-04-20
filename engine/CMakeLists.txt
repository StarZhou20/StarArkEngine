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

target_compile_definitions(StarArkEngine PUBLIC GLEW_STATIC)
