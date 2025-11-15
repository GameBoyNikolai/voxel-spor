cmake_minimum_required(VERSION 3.14...3.22)

project(
  voxel_spor
  VERSION 0.0.1
  LANGUAGES CXX
)

#
# CMake Config
#

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

# force a distinct build dir
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds not allowed. Please use a separate build directory")
endif()

# used for all external dependenices
include(FetchContent)

#
# Global Dependencies
#

# fmt
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG e424e3f2e607da02742f73db84873b8084fc714c  # 12.0.0
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
#   OPTIONS "FMT_INSTALL YES" # create an installable target
)
FetchContent_MakeAvailable(fmt)

# SDL3
FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.24
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
)
set(SDL_INSTALL TRUE)
set(SDL_TEST_LIBRARY FALSE)
set(SDL_LIBC FALSE)
FetchContent_MakeAvailable(SDL3)

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG a532f5b1cf27d6a3c099437e6959cf7e398a0a67  # 1.0.2
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(glm)

# STB
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG f1c79c02822848a9bed4315b12c8c8f3761e1296
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(stb)

# TinyOBJLoader
FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG e60d33385e2e4f7fa891513150f2532b5bbcb093
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(tinyobjloader)

# Vulkan
find_package(Vulkan COMPONENTS glslangValidator REQUIRED)

#
# Targets
#

add_subdirectory(voxel)
add_subdirectory(res)
add_subdirectory(viewer)
add_subdirectory(test)

#
# Target Folders
#

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

file(GLOB_RECURSE PROJECT_CMAKE_FILES "${CMAKE_SOURCE_DIR}/cmake/*.cmake")
add_custom_target(cmake-helpers SOURCES ${PROJECT_CMAKE_FILES})

set_target_properties(cmake-helpers PROPERTIES FOLDER "CMake")
set_target_properties(SDL3-shared PROPERTIES FOLDER "External")
set_target_properties(SDL_uclibc PROPERTIES FOLDER "External")
set_target_properties(fmt PROPERTIES FOLDER "External")
set_target_properties(glm PROPERTIES FOLDER "External")
set_target_properties(tinyobjloader PROPERTIES FOLDER "External")