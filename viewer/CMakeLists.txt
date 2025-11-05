cmake_minimum_required(VERSION 3.14...3.22)

#
# Executable
# 
set(EXE_NAME spor_viewer)

file(GLOB_RECURSE headers CONFIGURE_DEPENDS "include/viewer/*.h")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "src/*.cpp")

add_executable(
    ${EXE_NAME}
    ${headers} 
    ${sources}
    )

set_target_properties(${EXE_NAME} PROPERTIES FOLDER "Executables")

target_link_libraries(${EXE_NAME} PRIVATE voxel)
target_link_libraries(${EXE_NAME} PRIVATE fmt::fmt)
target_link_libraries(${EXE_NAME} PRIVATE SDL3::SDL3)
target_link_libraries(${EXE_NAME} PRIVATE Vulkan::Vulkan)

target_include_directories(${EXE_NAME} PRIVATE include)
target_include_directories(${EXE_NAME} PRIVATE SDL3::Headers)
target_include_directories(${EXE_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})
target_include_directories(${EXE_NAME} PRIVATE glm::Headers)
target_include_directories(${EXE_NAME} PRIVATE ${stb_SOURCE_DIR})

target_include_directories(${EXE_NAME} PRIVATE ${CMAKE_BINARY_DIR}/res)

add_dependencies(${EXE_NAME} shaders)

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