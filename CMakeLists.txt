cmake_minimum_required(VERSION 3.26)
project(lahn VERSION 0.1.0 LANGUAGES C CXX) # C: pl_mpeg

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
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.5.3
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
set(SUPPORT_FILEFORMAT_JPG ON CACHE BOOL "" FORCE) # lesson images: photos are usually JPG (PNG is on already)

# Optional decoders and effect nodes we don't use yet
set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "" FORCE)

# pl_mpeg: MPEG-1 video and MP2 audio in one public-domain header, for videos in lessons. It has no release tags,
# so it's pinned to a commit.
FetchContent_Declare(
    pl_mpeg
    GIT_REPOSITORY https://github.com/phoboslab/pl_mpeg.git
    GIT_TAG        c871f2be022ece7ef4f64230b4fb8e1fb9eb6023
)

FetchContent_MakeAvailable(raylib miniaudio imgui rlimgui doctest pl_mpeg)

# pl_mpeg is one header; its code is compiled once, in C, in a library of its own
add_library(pl_mpeg STATIC src/video/pl_mpeg.c)
target_include_directories(pl_mpeg PUBLIC ${pl_mpeg_SOURCE_DIR})

# Dear ImGui and rlImGui ship no CMake build of their own, so we compile their sources as one library
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp # ImGui::ShowDemoWindow(): every widget with its source, handy for learning
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp # ImGui::InputText for std::string
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/misc/cpp ${rlimgui_SOURCE_DIR})
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
add_library(lahn_core STATIC
    src/core/calibration.cpp
    src/core/chart.cpp
    src/core/drill.cpp
    src/core/exercisefile.cpp
    src/core/files.cpp
    src/core/intervals.cpp
    src/core/judge.cpp
    src/core/lesson.cpp
    src/core/music.cpp
    src/core/notation.cpp
    src/core/notedetector.cpp
    src/core/paths.cpp
    src/core/pitch.cpp
    src/core/routine.cpp
    src/core/score.cpp
    src/core/scales.cpp
    src/core/settings.cpp
    src/core/songlibrary.cpp
    src/core/synth.cpp
)
target_include_directories(lahn_core PUBLIC src)
enable_warnings(lahn_core)

# The game: audio, UI and screens on top of core
add_executable(lahn
    src/main.cpp
    src/audio/audio.cpp
    src/input/noteinput.cpp
    src/ui/theme.cpp
    src/ui/ui.cpp
    src/screens/calibration.cpp
    src/screens/editor.cpp
    src/screens/gameplay.cpp
    src/screens/learnscreen.cpp
    src/screens/lessoneditor.cpp
    src/learn/drillexercise.cpp
    src/learn/intervalexercise.cpp
    src/learn/lessonplayer.cpp
    src/learn/lessonview.cpp
    src/learn/playexercise.cpp
    src/learn/routineexercise.cpp
    src/screens/menus.cpp
    src/screens/settingsscreen.cpp
    src/screens/tuner.cpp
    src/views/highway.cpp
    src/views/noteviews.cpp
    src/views/staff.cpp
    src/views/tab.cpp
    src/video/video.cpp
)
target_link_libraries(lahn PRIVATE lahn_core raylib miniaudio imgui pl_mpeg)
enable_warnings(lahn)

# Copy game data next to the executable on every build (only files that changed are copied)
add_custom_target(copy_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            ${CMAKE_SOURCE_DIR}/resources
            $<TARGET_FILE_DIR:lahn>/resources
)
add_dependencies(lahn copy_resources)

# Tests: build, then run `ctest --test-dir build --output-on-failure`.
# They link only lahn_core, which also proves the core works without the game around it.
include(CTest)
if(BUILD_TESTING)
    add_executable(lahn_tests
        tests/main.cpp
        tests/calibration_test.cpp
        tests/chart_test.cpp
        tests/drill_test.cpp
        tests/exercisefile_test.cpp
        tests/intervals_test.cpp
        tests/judge_test.cpp
        tests/lesson_test.cpp
        tests/music_test.cpp
        tests/notation_test.cpp
        tests/notedetector_test.cpp
        tests/pitch_test.cpp
        tests/routine_test.cpp
        tests/score_test.cpp
        tests/scales_test.cpp
        tests/settings_test.cpp
        tests/synth_test.cpp
    )
    target_link_libraries(lahn_tests PRIVATE lahn_core doctest::doctest)
    target_compile_definitions(lahn_tests PRIVATE LAHN_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources/")
    enable_warnings(lahn_tests)
    add_test(NAME core_tests COMMAND lahn_tests)
endif()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
