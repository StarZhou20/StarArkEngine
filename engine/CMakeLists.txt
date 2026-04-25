file(GLOB_RECURSE ENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/debug/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rhi/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rhi/opengl/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rendering/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/serialization/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripting/*.cpp"
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

# --- Phase 15.F: C# (CoreCLR) MOD scripting --------------------------------
# OFF by default — building requires .NET SDK + nethost headers and the
# managed binding assembly. The engine still compiles when OFF; ScriptHost
# falls back to a no-op stub. See engine/scripting/ScriptHost.cpp.
option(STARARK_ENABLE_SCRIPTING "Enable C# MOD scripting via CoreCLR hosting" OFF)
if(STARARK_ENABLE_SCRIPTING)
    target_compile_definitions(StarArkEngine PUBLIC ARK_SCRIPTING_ENABLED=1)

    # ---- Locate the .NET runtime pack that ships nethost.{h,dll,lib} -----
    # Layout is <DOTNET_ROOT>/packs/Microsoft.NETCore.App.Host.<rid>/<ver>/runtimes/<rid>/native
    set(_dotnet_root "$ENV{DOTNET_ROOT}")
    if(NOT _dotnet_root)
        if(WIN32)
            set(_dotnet_root "C:/Program Files/dotnet")
        else()
            set(_dotnet_root "/usr/share/dotnet")
        endif()
    endif()
    if(WIN32)
        set(_rid "win-x64")
    else()
        set(_rid "linux-x64")
    endif()
    set(_pack_root "${_dotnet_root}/packs/Microsoft.NETCore.App.Host.${_rid}")
    if(NOT IS_DIRECTORY "${_pack_root}")
        message(FATAL_ERROR "STARARK_ENABLE_SCRIPTING=ON but DotNetAppHost pack "
                            "not found at: ${_pack_root}\n"
                            "Install .NET SDK (https://dotnet.microsoft.com/) "
                            "or set DOTNET_ROOT.")
    endif()
    file(GLOB _pack_versions RELATIVE "${_pack_root}" "${_pack_root}/*")
    list(SORT _pack_versions COMPARE NATURAL)
    list(REVERSE _pack_versions)
    list(GET _pack_versions 0 _pack_ver)
    set(_nethost_native "${_pack_root}/${_pack_ver}/runtimes/${_rid}/native")
    message(STATUS "StarArk: nethost native dir = ${_nethost_native}")

    target_include_directories(StarArkEngine PRIVATE "${_nethost_native}")
    if(WIN32)
        target_link_libraries(StarArkEngine PUBLIC "${_nethost_native}/nethost.lib")
        set(ARK_NETHOST_DLL "${_nethost_native}/nethost.dll" CACHE INTERNAL "Path to nethost.dll for runtime deploy")
    else()
        target_link_libraries(StarArkEngine PUBLIC "${_nethost_native}/libnethost.a")
    endif()

    # ---- Build the managed assembly via `dotnet build` -------------------
    # Output goes to <build>/managed/ which the exe targets copy beside
    # themselves into <exe>/scripting/. Re-runs whenever any .cs in the
    # project changes.
    set(_managed_proj "${CMAKE_SOURCE_DIR}/scripts/StarArk.Scripting/StarArk.Scripting.csproj")
    set(_managed_out  "${CMAKE_BINARY_DIR}/managed")
    file(MAKE_DIRECTORY "${_managed_out}")
    set(_managed_dll  "${_managed_out}/StarArk.Scripting.dll")
    file(GLOB _managed_sources "${CMAKE_SOURCE_DIR}/scripts/StarArk.Scripting/*.cs")
    add_custom_command(
        OUTPUT  "${_managed_dll}"
        COMMAND dotnet build "${_managed_proj}"
                -c Release
                -o "${_managed_out}"
                --nologo
        DEPENDS "${_managed_proj}"
                ${_managed_sources}
        VERBATIM
        COMMENT "Building StarArk.Scripting (managed)"
    )
    add_custom_target(StarArkManaged ALL DEPENDS "${_managed_dll}")
    add_dependencies(StarArkEngine StarArkManaged)
    set(ARK_MANAGED_OUT "${_managed_out}" CACHE INTERNAL "Managed output directory")
    set(ARK_MANAGED_DLL "${_managed_dll}" CACHE INTERNAL "Path to StarArk.Scripting.dll")
else()
    target_compile_definitions(StarArkEngine PUBLIC ARK_SCRIPTING_ENABLED=0)
endif()
