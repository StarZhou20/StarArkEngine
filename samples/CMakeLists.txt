file(GLOB_RECURSE SAMPLES_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(StarArkSamples ${SAMPLES_SOURCES})

if(WIN32 AND ARK_APP_ICON_RC)
    target_sources(StarArkSamples PRIVATE "${ARK_APP_ICON_RC}")
endif()

target_include_directories(StarArkSamples PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(StarArkSamples PRIVATE StarArkEngine)

# Copy samples content next to the exe for dev runs.
add_custom_command(TARGET StarArkSamples POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/content"
        "$<TARGET_FILE_DIR:StarArkSamples>/content"
)
add_custom_command(TARGET StarArkSamples POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:StarArkSamples>/mods"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:StarArkSamples>/logs"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/mods"
        "$<TARGET_FILE_DIR:StarArkSamples>/mods"
)

# Deploy engine shaders next to the exe so ShaderManager can load + hot-reload them.
add_custom_command(TARGET StarArkSamples POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ARK_ENGINE_SHADER_DIR}"
        "$<TARGET_FILE_DIR:StarArkSamples>/content/shaders"
)

# Deploy the active app icon next to the exe (see game/CMakeLists.txt).
if(ARK_APP_ICON)
    add_custom_command(TARGET StarArkSamples POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:StarArkSamples>/icon"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ARK_APP_ICON}"
            "$<TARGET_FILE_DIR:StarArkSamples>/icon/${ARK_APP_ICON_NAME}"
    )
endif()

# Deploy the managed scripting runtime (see game/CMakeLists.txt).
if(STARARK_ENABLE_SCRIPTING)
    add_custom_command(TARGET StarArkSamples POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:StarArkSamples>/scripting"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ARK_MANAGED_OUT}"
            "$<TARGET_FILE_DIR:StarArkSamples>/scripting"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ARK_NETHOST_DLL}"
            "$<TARGET_FILE_DIR:StarArkSamples>/nethost.dll"
    )

    # ---- Sample MOD: HelloMod -----------------------------------------
    # Built as a separate `dotnet build` referencing StarArk.Scripting.dll.
    # Output: <build>/managed-mods/hellomod/HelloMod.dll
    # Deploy: <exe>/mods/hellomod/scripts/HelloMod.dll
    set(_hellomod_proj "${CMAKE_CURRENT_SOURCE_DIR}/mods-src/hellomod/HelloMod.csproj")
    set(_hellomod_out  "${CMAKE_BINARY_DIR}/managed-mods/hellomod")
    set(_hellomod_dll  "${_hellomod_out}/HelloMod.dll")
    file(MAKE_DIRECTORY "${_hellomod_out}")
    add_custom_command(
        OUTPUT  "${_hellomod_dll}"
        COMMAND dotnet build "${_hellomod_proj}"
                -c Release
                -o "${_hellomod_out}"
                "-p:ArkScriptingRef=${ARK_MANAGED_DLL}"
                --nologo
        DEPENDS "${_hellomod_proj}"
                "${CMAKE_CURRENT_SOURCE_DIR}/mods-src/hellomod/HelloMod.cs"
                "${ARK_MANAGED_DLL}"
        VERBATIM
        COMMENT "Building sample MOD: HelloMod (managed)"
    )
    add_custom_target(StarArkSamples_HelloMod ALL DEPENDS "${_hellomod_dll}")
    add_dependencies(StarArkSamples StarArkSamples_HelloMod)

    add_custom_command(TARGET StarArkSamples POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:StarArkSamples>/mods/hellomod/scripts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_hellomod_dll}"
            "$<TARGET_FILE_DIR:StarArkSamples>/mods/hellomod/scripts/HelloMod.dll"
    )
endif()

# ---------------------------------------------------------------------------
# v0.3 ModSpec — StarArkLauncher (.NET 10 WPF) deployment.
# Builds tools/Launcher/ via `dotnet publish`, then copies the entire
# self-contained-ish output next to StarArkSamples.exe so launching from
# build/samples/ works end-to-end (StarArkLauncher.exe sits beside
# StarArkSamples.exe and scans ./mods).
# ---------------------------------------------------------------------------
set(_launcher_proj   "${CMAKE_SOURCE_DIR}/tools/Launcher/Launcher.csproj")
set(_launcher_outdir "${CMAKE_BINARY_DIR}/launcher-publish")
set(_launcher_marker "${_launcher_outdir}/StarArkLauncher.exe")

add_custom_command(
    OUTPUT  "${_launcher_marker}"
    COMMAND dotnet publish "${_launcher_proj}"
            -c Release
            -o "${_launcher_outdir}"
            --nologo
    DEPENDS "${_launcher_proj}"
            "${CMAKE_SOURCE_DIR}/tools/Launcher/MainWindow.xaml"
            "${CMAKE_SOURCE_DIR}/tools/Launcher/MainWindow.xaml.cs"
            "${CMAKE_SOURCE_DIR}/tools/Launcher/Themes/Dark.xaml"
            "${CMAKE_SOURCE_DIR}/tools/Launcher/Services/LaunchService.cs"
            "${CMAKE_SOURCE_DIR}/tools/Launcher/Services/ModScanner.cs"
            "${CMAKE_SOURCE_DIR}/tools/Launcher/Services/Validator.cs"
            "${CMAKE_SOURCE_DIR}/tools/Launcher/Models/ModInfo.cs"
            "${CMAKE_SOURCE_DIR}/tools/Launcher/Models/LauncherConfig.cs"
            "${CMAKE_SOURCE_DIR}/tools/Launcher/ViewModels/MainViewModel.cs"
    VERBATIM
    COMMENT "Building StarArkLauncher (.NET 10 WPF)"
)
add_custom_target(StarArkLauncher_Publish ALL DEPENDS "${_launcher_marker}")
add_dependencies(StarArkSamples StarArkLauncher_Publish)

add_custom_command(TARGET StarArkSamples POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${_launcher_outdir}"
        "$<TARGET_FILE_DIR:StarArkSamples>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/launcher.config.json"
        "$<TARGET_FILE_DIR:StarArkSamples>/launcher.config.json"
    COMMENT "Deploying StarArkLauncher next to StarArkSamples.exe"
)
