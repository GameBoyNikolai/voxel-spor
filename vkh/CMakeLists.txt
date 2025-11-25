cmake_minimum_required(VERSION 3.14...3.22)

#
# Library
# 
set(LIB_NAME vkh)

file(GLOB_RECURSE headers CONFIGURE_DEPENDS "include/vkh/*.h")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "src/*.cpp")

add_library(
    ${LIB_NAME}
    ${headers} 
    ${sources}
    )

set_target_properties(${LIB_NAME} PROPERTIES FOLDER "Libraries")

target_include_directories(${LIB_NAME} PUBLIC include)
target_include_directories(${LIB_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})
target_include_directories(${LIB_NAME} PRIVATE SDL3::Headers)

target_link_libraries(${LIB_NAME} PRIVATE fmt::fmt)
target_link_libraries(${LIB_NAME} PRIVATE Vulkan::Vulkan)
target_link_libraries(${LIB_NAME} PRIVATE SDL3::SDL3)

