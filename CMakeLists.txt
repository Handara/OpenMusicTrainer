cmake_minimum_required(VERSION 3.26)
project(OpenMusicTrainer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# raylib handles graphics, window and input only. Audio goes through miniaudio directly (audio.cpp),
# which gives us capture devices and full control that raylib's audio module doesn't expose.
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)

# Optional decoders and effect nodes we don't use yet
set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(raylib miniaudio)

add_executable(OpenMusicTrainer main.cpp chart.cpp audio.cpp)
target_link_libraries(OpenMusicTrainer PUBLIC raylib miniaudio)

# Copy game data next to the executable on every build (only files that changed are copied)
add_custom_target(copy_resources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            ${CMAKE_SOURCE_DIR}/Ressources
            $<TARGET_FILE_DIR:OpenMusicTrainer>/Ressources
)
add_dependencies(OpenMusicTrainer copy_resources)

include(CTest)
enable_testing()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
