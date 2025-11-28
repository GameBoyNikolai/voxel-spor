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

target_link_libraries(${EXE_NAME} PRIVATE vkh)
target_link_libraries(${EXE_NAME} PRIVATE voxel)
target_link_libraries(${EXE_NAME} PRIVATE fmt::fmt)
target_link_libraries(${EXE_NAME} PRIVATE SDL3::SDL3)
target_link_libraries(${EXE_NAME} PRIVATE Vulkan::Vulkan)
target_link_libraries(${EXE_NAME} PRIVATE GTest::gtest_main)

target_include_directories(${EXE_NAME} PRIVATE include)
target_include_directories(${EXE_NAME} PRIVATE glm::Headers)
target_include_directories(${EXE_NAME} PRIVATE SDL3::Headers)
target_include_directories(${EXE_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})
target_include_directories(${EXE_NAME} PRIVATE ${stb_SOURCE_DIR})

#
# Target Folders
#

set_target_properties(gmock PROPERTIES FOLDER "External")
set_target_properties(gmock_main PROPERTIES FOLDER "External")
set_target_properties(gtest PROPERTIES FOLDER "External")
set_target_properties(gtest_main PROPERTIES FOLDER "External")

#
# Copy DLLs
#
if (WIN32)
  add_custom_command(TARGET ${EXE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${EXE_NAME}>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:SDL3::SDL3>
      $<TARGET_FILE_DIR:${EXE_NAME}>
    COMMENT "Copying SDL3 to executable folder"
  )
endif()

#
# Discover Tests
#

include(GoogleTest)
gtest_discover_tests(${EXE_NAME})