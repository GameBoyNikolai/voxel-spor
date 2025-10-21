#
#  CompileSpirvShader.cmake
#
#  Created by Bradley Austin Davis on 2016/06/23
#
#  Distributed under the Apache License, Version 2.0.
#  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
#

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

