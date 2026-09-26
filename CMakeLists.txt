cmake_minimum_required(VERSION 3.26)
project(OpenMusicTrainer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Without a build type CMake compiles unoptimized, and real-time audio analysis is ~4x slower that way.
# Default to optimized code that still has debug info. (Visual Studio picks the type at build time: --config)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
)
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.25
    GIT_SHALLOW    TRUE
)
# rlImGui (Dear ImGui's raylib backend) publishes a tested ImGui version per raylib version
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.7
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(
    rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        Raylib_5_5
    GIT_SHALLOW    TRUE
)

# raylib handles graphics, window and input only. Audio goes through miniaudio directly (audio.cpp),
# which gives us capture devices and full control that raylib's audio module doesn't expose.
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)

# Optional decoders and effect nodes we don't use yet
set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(raylib miniaudio imgui rlimgui)

# Dear ImGui and rlImGui ship no CMake build of their own, so we compile their sources as one library
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp # ImGui::ShowDemoWindow(): every widget with its source, handy for learning
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${rlimgui_SOURCE_DIR})
target_link_libraries(imgui PUBLIC raylib)

# Warnings for our own code only (third-party libraries keep their own settings)
function(enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# Core: pure C++ (chart format, music theory, pitch detection, song library).
# It deliberately does NOT link raylib, miniaudio or imgui: including one of them here fails to compile,
# which keeps this layer reusable (editor, tests, a future server) and testable on its own.
add_library(omt_core STATIC
    src/core/chart.cpp
    src/core/music.cpp
    src/core/pitch.cpp
    src/core/songlibrary.cpp
)
target_include_directories(omt_core PUBLIC src)
enable_warnings(omt_core)

# The game: audio, UI and screens on top of core
add_executable(OpenMusicTrainer
    src/main.cpp
    src/audio/audio.cpp
    src/ui/ui.cpp
    src/screens/gameplay.cpp
    src/screens/menus.cpp
    src/screens/tuner.cpp
)
target_link_libraries(OpenMusicTrainer PRIVATE omt_core raylib miniaudio imgui)
enable_warnings(OpenMusicTrainer)

# Copy game data next to the executable on every build (only files that changed are copied)
add_custom_target(copy_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            ${CMAKE_SOURCE_DIR}/resources
            $<TARGET_FILE_DIR:OpenMusicTrainer>/resources
    # UI font that ships with Dear ImGui (Apache 2.0 license)
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:OpenMusicTrainer>/resources/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf
            $<TARGET_FILE_DIR:OpenMusicTrainer>/resources/fonts/Roboto-Medium.ttf
)
add_dependencies(OpenMusicTrainer copy_resources)

include(CTest)
enable_testing()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
