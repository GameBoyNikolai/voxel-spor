cmake_minimum_required(VERSION 3.14...3.22)

#
# Dependencies
#

# Enable testing via CTest
enable_testing()

# GTest
FetchContent_Declare(
  gtest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        52eb8108c5bdec04579160ae17225d66034bd723
)
FetchContent_MakeAvailable(gtest)

#
# Executable
# 
set(EXE_NAME test)

file(GLOB_RECURSE headers CONFIGURE_DEPENDS "include/test/*.h")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "src/*.cpp")

add_executable(
    ${EXE_NAME}
    ${headers} 
    ${sources}
    )

set_target_properties(${EXE_NAME} PROPERTIES FOLDER "Executables")

target_link_libraries(${EXE_NAME} PRIVATE voxel)
target_link_libraries(${EXE_NAME} PRIVATE GTest::gtest_main)

target_include_directories(${EXE_NAME} PRIVATE include)

#
# Discover Tests
#

include(GoogleTest)
gtest_discover_tests(${EXE_NAME})

#
# Target Folders
#

set_target_properties(gmock PROPERTIES FOLDER "External")
set_target_properties(gmock_main PROPERTIES FOLDER "External")
set_target_properties(gtest PROPERTIES FOLDER "External")
set_target_properties(gtest_main PROPERTIES FOLDER "External")