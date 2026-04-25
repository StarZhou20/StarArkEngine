add_executable(StarArkGame
    src/main.cpp
)
if(WIN32 AND ARK_APP_ICON_RC)
    target_sources(StarArkGame PRIVATE "${ARK_APP_ICON_RC}")
endif()
target_include_directories(StarArkGame PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(StarArkGame PRIVATE StarArkEngine)

# Copy game content next to the executable after build so Paths::Content()
# resolves correctly when running from the build directory.
add_custom_command(TARGET StarArkGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/content"
        "$<TARGET_FILE_DIR:StarArkGame>/content"
)
add_custom_command(TARGET StarArkGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/game.config.json"
        "$<TARGET_FILE_DIR:StarArkGame>/game.config.json"
)
# Create empty mods/ and logs/ beside exe so runtime directories exist.
add_custom_command(TARGET StarArkGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:StarArkGame>/mods"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:StarArkGame>/logs"
)

# Deploy engine shaders next to the exe so ShaderManager can load + hot-reload them.
add_custom_command(TARGET StarArkGame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${ARK_ENGINE_SHADER_DIR}"
        "$<TARGET_FILE_DIR:StarArkGame>/content/shaders"
)

# Deploy the active app icon next to the exe so Window::SetIconFromFile can
# load it at runtime (top-left window icon). The filename is whatever lives
# under icon/ — kept dynamic so it can be swapped without code changes.
if(ARK_APP_ICON)
    add_custom_command(TARGET StarArkGame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:StarArkGame>/icon"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ARK_APP_ICON}"
            "$<TARGET_FILE_DIR:StarArkGame>/icon/${ARK_APP_ICON_NAME}"
    )
endif()

# Deploy the managed scripting runtime if enabled. ScriptHost loads from
# <exe>/scripting/StarArk.Scripting.dll and needs nethost.dll alongside the
# exe to bootstrap CoreCLR.
if(STARARK_ENABLE_SCRIPTING)
    add_custom_command(TARGET StarArkGame POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:StarArkGame>/scripting"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ARK_MANAGED_OUT}"
            "$<TARGET_FILE_DIR:StarArkGame>/scripting"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ARK_NETHOST_DLL}"
            "$<TARGET_FILE_DIR:StarArkGame>/nethost.dll"
    )
endif()
