cmake_minimum_required(VERSION 3.14...3.22)

project(voxel_spor_tests LANGUAGES CXX)

# ---- Options ----

option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

# --- Import tools ----

include(../cmake/tools.cmake)

# ---- Dependencies ----

include(../cmake/CPM.cmake)

CPMAddPackage(
  NAME doctest
  GIT_TAG b7c21ec5ceeadb4951b00396fc1e4642dd347e5f  # 2.4.9
  GITHUB_REPOSITORY doctest/doctest
)

CPMAddPackage(
  NAME Format.cmake
  GIT_TAG 32e02a34581cec87f919d8fd126b09a8ebdd480c  # 1.7.3
  GITHUB_REPOSITORY TheLartians/Format.cmake
)

if(TEST_INSTALLED_VERSION)
  find_package(voxel_spor REQUIRED)
else()
  CPMAddPackage(NAME voxel_spor SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

# ---- Create binary ----

file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_executable(${PROJECT_NAME} ${sources})
target_link_libraries(${PROJECT_NAME} doctest::doctest voxel_spor::voxel_spor)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

# enable compiler warnings
if(NOT TEST_INSTALLED_VERSION)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(voxel_spor PUBLIC -Wall -Wpedantic -Wextra -Werror)
  elseif(MSVC)
    target_compile_options(voxel_spor PUBLIC /W4 /WX)
    target_compile_definitions(${PROJECT_NAME} PUBLIC DOCTEST_CONFIG_USE_STD_HEADERS)
  endif()
endif()

# ---- Add voxel_spor-tests ----

enable_testing()

# Note: doctest and similar testing frameworks can automatically configure CMake tests. For other
# testing frameworks add the tests target instead: add_test(NAME ${PROJECT_NAME} COMMAND
# ${PROJECT_NAME})

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(${PROJECT_NAME})

# ---- code coverage ----

if(ENABLE_TEST_COVERAGE)
  target_compile_options(voxel_spor PUBLIC -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(voxel_spor PUBLIC -fprofile-arcs -ftest-coverage)
endif()
