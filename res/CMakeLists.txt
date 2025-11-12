cmake_minimum_required(VERSION 3.14...3.22)

#
# Shaders
#

include(shader_helpers)

message("Spir-V Compiler: ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")

set(SHADER_DIR "shaders")
file(GLOB_RECURSE SHADERS
    ${SHADER_DIR}/*.vert
    ${SHADER_DIR}/*.frag
    ${SHADER_DIR}/*.comp
    ${SHADER_DIR}/*.tesc
    ${SHADER_DIR}/*.tese
    ${SHADER_DIR}/*.geom
)

add_custom_target(shaders SOURCES ${SHADERS})

set_target_properties(shaders PROPERTIES FOLDER "Libraries")

foreach(SHADER_FILE ${SHADERS})
    set(BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    compile_shader(${BASE_DIR} ${SHADER_FILE})
    file(RELATIVE_PATH DEST_SHADER ${BASE_DIR} ${SHADER_FILE})
    set(SHADER_HEADER_NAME "${CMAKE_BINARY_DIR}/res/${DEST_SHADER}.inl")
    target_sources(shaders PRIVATE ${SHADER_HEADER_NAME})
endforeach()