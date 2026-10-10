cmake_minimum_required(VERSION 3.20)
project(2Fast2Rally VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/json-for-moderncpp")
set(ENET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/enet")

# raylib: prebuilt MinGW binaries vendored on Windows (same as Prairie);
# elsewhere use a system install or fetch 6.0 from source.
if(WIN32)
    set(RAYLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/raylib-6.0_win64_mingw-w64")
else()
    find_package(raylib 6.0 QUIET)
    if(NOT raylib_FOUND)
        include(FetchContent)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(raylib GIT_REPOSITORY "https://github.com/raysan5/raylib.git" GIT_TAG "6.0" GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(raylib)
    endif()
endif()

add_subdirectory("${ENET_DIR}" EXCLUDE_FROM_ALL)

set(RALLY_SOURCES
    src/main.cpp
    src/core/physicsEngine.cpp
    src/core/networkManager.cpp
    src/core/netAddr.cpp
    src/core/log.cpp
    src/core/crashHandler.cpp
    src/core/settings.cpp
    src/core/input.cpp
    src/core/audioSynth.cpp
    src/core/ui.cpp
    src/core/noise.cpp
    src/game/carSpec.cpp
    src/game/vehicle.cpp
    src/game/stage.cpp
    src/game/stageGen.cpp
    src/game/paceNotes.cpp
    src/game/voice.cpp
    src/game/world.cpp
    src/game/carRender.cpp
    src/game/particles.cpp
    src/game/session.cpp
    src/game/leaderboard.cpp
    src/game/game.cpp
    src/game/gameMenus.cpp
    src/game/gameStage.cpp
    src/game/gameHud.cpp
    src/game/editor.cpp
)

add_executable(2Fast2Rally ${RALLY_SOURCES})
target_include_directories(2Fast2Rally PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${JSON_DIR}/include"
    "${ENET_DIR}/include"
)
target_link_libraries(2Fast2Rally PRIVATE enet)
target_compile_definitions(2Fast2Rally PRIVATE RALLY_VERSION="${PROJECT_VERSION}")

if(WIN32)
    target_include_directories(2Fast2Rally PRIVATE "${RAYLIB_DIR}/include")
    target_link_directories(2Fast2Rally PRIVATE "${RAYLIB_DIR}/lib")
    target_link_libraries(2Fast2Rally PRIVATE raylib ws2_32 opengl32 gdi32 winmm shell32 user32 dbghelp)
    # Static runtime so the zip needs no MinGW DLLs next to the exe.
    target_link_options(2Fast2Rally PRIVATE -static -static-libgcc -static-libstdc++)
    option(RALLY_SHOW_CONSOLE "Keep a console window for logs" OFF)
    if(NOT RALLY_SHOW_CONSOLE)
        set_target_properties(2Fast2Rally PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
else()
    target_link_libraries(2Fast2Rally PRIVATE raylib)
endif()

# Assets load relative to the exe; mirror res/ next to it.
add_custom_command(TARGET 2Fast2Rally POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/res" "$<TARGET_FILE_DIR:2Fast2Rally>/res"
    COMMENT "Mirroring res/ next to the executable"
    VERBATIM
)
