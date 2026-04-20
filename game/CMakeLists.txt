add_executable(StarArkGame
    main.cpp
    scenes/DemoScene.cpp
    scenes/FBXDemoScene.cpp
)
target_include_directories(StarArkGame PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(StarArkGame PRIVATE StarArkEngine)
