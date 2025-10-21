cmake_minimum_required(VERSION 3.14...3.22)

include(../cmake/GroupSources.cmake)
include(../cmake/CompileSpirvShader.cmake)
include(../cmake/shader_helpers.cmake)

set(SHADER_DIR "shaders")
file(GLOB_RECURSE SHADERS
    ${SHADER_DIR}/*.vert
    ${SHADER_DIR}/*.frag
    ${SHADER_DIR}/*.comp
    ${SHADER_DIR}/*.tesc
    ${SHADER_DIR}/*.tese
    ${SHADER_DIR}/*.geom
)
GroupSources("res/shaders")

add_custom_target(shaders SOURCES ${SHADERS})

foreach(SHADER_FILE ${SHADERS})
    compile_shader(${SHADER_FILE})
    file(RELATIVE_PATH DEST_SHADER ${CMAKE_SOURCE_DIR}/.. ${SHADER_FILE})
    message("source location: ${CMAKE_BINARY_DIR}/${DEST_SHADER}.inl")
    set(SHADER_HEADER_NAME "${CMAKE_BINARY_DIR}/${DEST_SHADER}.inl")
    target_sources(shaders PRIVATE ${SHADER_HEADER_NAME})
endforeach()