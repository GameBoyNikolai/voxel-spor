cmake_minimum_required(VERSION 3.14...3.22)

#
# Library
# 
set(LIB_NAME voxel)

file(GLOB_RECURSE headers CONFIGURE_DEPENDS "include/voxel/*.h")
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "src/*.cpp")

add_library(
    ${LIB_NAME}
    ${headers} 
    ${sources}
    )

set_target_properties(${LIB_NAME} PROPERTIES FOLDER "Libraries")

target_include_directories(${LIB_NAME} PUBLIC include)