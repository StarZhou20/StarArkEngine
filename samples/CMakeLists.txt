file(GLOB_RECURSE SAMPLES_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(StarArkSamples ${SAMPLES_SOURCES})

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
