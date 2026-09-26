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
FetchContent_MakeAvailable(raylib)

add_executable(OpenMusicTrainer main.cpp chart.cpp)
target_link_libraries(OpenMusicTrainer PUBLIC raylib)

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
