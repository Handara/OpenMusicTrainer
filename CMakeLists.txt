cmake_minimum_required(VERSION 3.10.0)
project(OpenMusicTrainer VERSION 0.1.0 LANGUAGES C)

include(FetchContent)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
)
FetchContent_MakeAvailable(raylib)

add_executable(OpenMusicTrainer main.c)
target_link_libraries(OpenMusicTrainer PUBLIC raylib)

include(CTest)
enable_testing()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
