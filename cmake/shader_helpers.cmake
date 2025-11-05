function(COMPILE_SPIRV_SHADER SHADER_FILE)
    # Define the final name of the generated shader file
    get_filename_component(SHADER_TARGET ${SHADER_FILE} NAME_WE)
    get_filename_component(SHADER_EXT ${SHADER_FILE} EXT)
    #set(COMPILE_OUTPUT "${SHADER_FILE}.spv")
    set(SPIRV_OUTPUT "${SHADER_FILE}.spv")
    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V ${SHADER_FILE} -o ${SPIRV_OUTPUT}
        DEPENDS ${SHADER_FILE})
    set(COMPILE_SPIRV_SHADER_RETURN ${SPIRV_OUTPUT} PARENT_SCOPE)
endfunction()


function(COMPILE_SHADER BASE_DIR SHADER_FILE)
    file(RELATIVE_PATH DEST_SHADER ${BASE_DIR} ${SHADER_FILE})
    set(SHADER_HEADER_NAME "${CMAKE_BINARY_DIR}/res/${DEST_SHADER}.inl")
    get_source_file_property(SHADER_EXISTS ${SHADER_HEADER_NAME} GENERATED)
    get_filename_component(SHADER_FOLDER ${DEST_SHADER} DIRECTORY)
    if (SHADER_EXISTS)
        message(DEBUG "Shader ${SHADER_FILE} already has build rules. Skipping.")
        return()
    endif()
    message(DEBUG "Compiling shader ${SHADER_FILE}")
    compile_spirv_shader(${SHADER_FILE})
    message(DEBUG "\tCompiled file ${COMPILE_SPIRV_SHADER_RETURN}")
    message(DEBUG "\tHeader file ${SHADER_HEADER_NAME}")
    add_custom_command(
        COMMAND ${CMAKE_COMMAND} -DCONFIG_FILE="${CMAKE_SOURCE_DIR}/cmake/ShaderData.inl.in" -DTARGET_NAME="${SHADER_FOLDER}" -DSHADER_FILE="${SHADER_FILE}" -DSHADER_SPIRV="${COMPILE_SPIRV_SHADER_RETURN}" -DSHADER_HEADER_NAME="${SHADER_HEADER_NAME}" -P ${CMAKE_SOURCE_DIR}/cmake/CreateShaderHeader.cmake
        OUTPUT ${SHADER_HEADER_NAME}
        DEPENDS ${COMPILE_SPIRV_SHADER_RETURN}
        COMMENT "Making Shader Header ${SHADER_HEADER_NAME}"
    )
    source_group("Shaders" FILES ${SHADER_FILE})
    source_group("Compiled Shaders" FILES ${SHADER_HEADER_NAME})
endfunction()
