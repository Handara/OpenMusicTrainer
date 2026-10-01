cmake_minimum_required(VERSION 3.26)
project(lahn VERSION 0.1.0 LANGUAGES C CXX) # C: miniaudio, pl_mpeg

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
    SOURCE_SUBDIR  none # only the files: we compile it ourselves, below, to add Ogg Vorbis
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

# pl_mpeg: MPEG-1 video and MP2 audio in one public-domain header, for videos in lessons. It has no release tags,
# so it's pinned to a commit.
FetchContent_Declare(
    pl_mpeg
    GIT_REPOSITORY https://github.com/phoboslab/pl_mpeg.git
    GIT_TAG        c871f2be022ece7ef4f64230b4fb8e1fb9eb6023
)

# miniz: zip files, for song packages (core/songpackage). Pure C, MIT licensed. Used as a sub-project it builds
# only its library (no examples, tests or install).
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG        3.1.2
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(raylib miniaudio imgui rlimgui doctest pl_mpeg miniz)

# Windows: Steinberg's ASIO SDK, for the audio interfaces' own low-latency drivers (audio/asiodriver.cpp). lahn takes
# it under its GPLv3 option. Pinned by checksum to Steinberg's own package; its host helpers are compiled here.
if(WIN32)
    FetchContent_Declare(
        asiosdk
        URL      https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip
        URL_HASH SHA256=d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca
        SOURCE_SUBDIR none
    )
    FetchContent_MakeAvailable(asiosdk)
    add_library(asiosdk STATIC
        ${asiosdk_SOURCE_DIR}/common/asio.cpp
        ${asiosdk_SOURCE_DIR}/host/asiodrivers.cpp
        ${asiosdk_SOURCE_DIR}/host/pc/asiolist.cpp
    )
    target_include_directories(asiosdk PUBLIC ${asiosdk_SOURCE_DIR}/common ${asiosdk_SOURCE_DIR}/host ${asiosdk_SOURCE_DIR}/host/pc)
    target_link_libraries(asiosdk PUBLIC ole32 advapi32 user32)
endif()

# miniaudio is one header too, compiled the same way, with stb_vorbis (in its extras) for .ogg songs. On Linux and
# macOS it needs threads, dlopen (it loads the system's audio libraries when it starts) and the math library.
add_library(miniaudio STATIC src/audio/miniaudio.c)
target_include_directories(miniaudio PUBLIC ${miniaudio_SOURCE_DIR})
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

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
        # getenv, fopen, strncpy: standard C, which Visual Studio flags in favour of its own versions
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# Core: pure C++ (chart format, music theory, pitch detection, song library).
# It deliberately does NOT link raylib, miniaudio or imgui: including one of them here fails to compile,
# which keeps this layer reusable (editor, tests, a future server) and testable on its own.
add_library(lahn_core STATIC
    src/core/addon.cpp
    src/core/backing.cpp
    src/core/calibration.cpp
    src/core/chart.cpp
    src/core/chords.cpp
    src/core/drill.cpp
    src/core/exercisefile.cpp
    src/core/files.cpp
    src/core/fretboard.cpp
    src/core/guitarpro.cpp
    src/core/inputs.cpp
    src/core/intervals.cpp
    src/core/judge.cpp
    src/core/lesson.cpp
    src/core/midi.cpp
    src/core/music.cpp
    src/core/notation.cpp
    src/core/notedetector.cpp
    src/core/paths.cpp
    src/core/pianokeys.cpp
    src/core/pitch.cpp
    src/core/positions.cpp
    src/core/quiz.cpp
    src/core/ranking.cpp
    src/core/reading.cpp
    src/core/rhythm.cpp
    src/core/rhythmmode.cpp
    src/core/routine.cpp
    src/core/sampleformat.cpp
    src/core/tonechain.cpp
    src/core/transcribe.cpp
    src/core/tonelibrary.cpp
    src/core/xml.cpp
    src/core/tuningcheck.cpp
    src/core/score.cpp
    src/core/scales.cpp
    src/core/settings.cpp
    src/core/singing.cpp
    src/core/songlibrary.cpp
    src/core/songpackage.cpp
    src/core/stemsplit.cpp
    src/core/synth.cpp
    src/core/today.cpp
)
target_include_directories(lahn_core PUBLIC src)
target_link_libraries(lahn_core PRIVATE miniz) # zip files for song packages: plain C, no graphics or audio
enable_warnings(lahn_core)

# The game: audio, UI and screens on top of core
add_executable(lahn
    src/main.cpp
    src/app/crashreport.cpp
    src/app/filedialog.cpp
    src/app/stemmodel.cpp
    src/audio/audio.cpp
    src/audio/asiodriver.cpp
    src/input/midi.cpp
    src/input/noteinput.cpp
    src/input/pianokeys.cpp
    src/input/synthmonitor.cpp
    src/ui/hitfeedback.cpp
    src/ui/menulist.cpp
    src/ui/fretboardview.cpp
    src/ui/pianoview.cpp
    src/ui/rungraph.cpp
    src/ui/settingsui.cpp
    src/ui/theme.cpp
    src/ui/transition.cpp
    src/ui/ui.cpp
    src/screens/calibration.cpp
    src/screens/editor.cpp
    src/screens/gameplay.cpp
    src/screens/importsong.cpp
    src/screens/instrumentscreen.cpp
    src/screens/learnscreen.cpp
    src/screens/lessoneditor.cpp
    src/screens/mainmenu.cpp
    src/learn/chordexercise.cpp
    src/learn/drillexercise.cpp
    src/learn/fretboardexercise.cpp
    src/learn/intervalexercise.cpp
    src/learn/lessonplayer.cpp
    src/learn/lessonview.cpp
    src/learn/playexercise.cpp
    src/learn/routineexercise.cpp
    src/learn/singingexercise.cpp
    src/screens/menus.cpp
    src/screens/newsong.cpp
    src/screens/settingsscreen.cpp
    src/screens/tuner.cpp
    src/screens/tuningscreen.cpp
    src/screens/tonewizard.cpp
    src/views/neckview.cpp
    src/views/noteviews.cpp
    src/views/pianohighway.cpp
    src/views/rhythmlane.cpp
    src/views/smooth.cpp
    src/views/staff.cpp
    src/views/viewfont.cpp
    src/video/video.cpp
)
target_link_libraries(lahn PRIVATE lahn_core raylib miniaudio imgui pl_mpeg ${CMAKE_DL_LIBS}) # dl: the stems add-on's runtime, loaded when it's there
# ONNX Runtime's interface only (its headers): the library itself comes with the stems add-on (src/app/stemmodel.cpp)
target_include_directories(lahn SYSTEM PRIVATE third_party/onnxruntime)
if(WIN32)
    target_link_libraries(lahn PRIVATE winmm dbghelp comdlg32 asiosdk) # MIDI input (input/midi.cpp); crash reports (app/crashreport.cpp); the Open dialog (app/filedialog.cpp); ASIO
endif()
enable_warnings(lahn)

# Makes the sample song's chart and audio (tools/make_song.cpp). Not built with the game: build and run it by hand.
add_executable(make_song EXCLUDE_FROM_ALL tools/make_song.cpp)
target_link_libraries(make_song PRIVATE lahn_core)
enable_warnings(make_song)

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
        tests/addon_test.cpp
        tests/calibration_test.cpp
        tests/chart_test.cpp
        tests/chords_test.cpp
        tests/drill_test.cpp
        tests/guitarpro_test.cpp
        tests/exercisefile_test.cpp
        tests/fretboard_test.cpp
        tests/inputs_test.cpp
        tests/intervals_test.cpp
        tests/judge_test.cpp
        tests/lesson_test.cpp
        tests/midi_test.cpp
        tests/music_test.cpp
        tests/notation_test.cpp
        tests/notedetector_test.cpp
        tests/pianokeys_test.cpp
        tests/pitch_test.cpp
        tests/positions_test.cpp
        tests/ranking_test.cpp
        tests/reading_test.cpp
        tests/rhythm_test.cpp
        tests/rhythmmode_test.cpp
        tests/routine_test.cpp
        tests/score_test.cpp
        tests/scales_test.cpp
        tests/settings_test.cpp
        tests/singing_test.cpp
        tests/songlibrary_test.cpp
        tests/sampleformat_test.cpp
        tests/songpackage_test.cpp
        tests/stemsplit_test.cpp
        tests/synth_test.cpp
        tests/tonechain_test.cpp
        tests/transcribe_test.cpp
        tests/tuningcheck_test.cpp
        tests/today_test.cpp
    )
    target_link_libraries(lahn_tests PRIVATE lahn_core doctest::doctest miniz) # miniz: to make broken packages
    target_compile_definitions(lahn_tests PRIVATE LAHN_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/resources/")
    enable_warnings(lahn_tests)
    add_test(NAME core_tests COMMAND lahn_tests)
endif()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
