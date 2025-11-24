#include "viewer/vulkan_compute.h"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace spor::vk {

namespace {
VkDescriptorType to_desc_type(Kernel::ParamType type) {
    switch (type) {
        case Kernel::ParamType::kUBO:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case Kernel::ParamType::kSSBO:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case Kernel::ParamType::kStorageImage:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }

    throw std::invalid_argument("Invalid Kernel ParamType");
}
}  // namespace

Kernel::~Kernel() {}

Kernel::ptr Kernel::create(SurfaceDevice::ptr device, const std::vector<uint32_t>& compiled_shader,
                           const std::vector<ParamType> param_types) {
    VkShaderModuleCreateInfo shader_module_info{};
    shader_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_module_info.codeSize = compiled_shader.size() * sizeof(uint32_t);
    shader_module_info.pCode = compiled_shader.data();

    VkShaderModule shader_module;
    helpers::check_vulkan(
        vkCreateShaderModule(*device, &shader_module_info, nullptr, &shader_module));

    VkPipelineShaderStageCreateInfo shader_stage_info{};
    shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;

    shader_stage_info.module = shader_module;
    shader_stage_info.pName = "main";

    std::vector<VkDescriptorSetLayoutBinding> set_layouts;

    for (const auto& param : param_types) {
        auto param_desc = to_desc_type(param);

        auto& layout = set_layouts.emplace_back();
        layout.binding = static_cast<uint32_t>(set_layouts.size() - 1);
        layout.descriptorType = param_desc;
        layout.descriptorCount = 1;
        layout.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        layout.pImmutableSamplers = nullptr;  // Optional
    }

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(set_layouts.size());
    layout_info.pBindings = set_layouts.data();

    VkDescriptorSetLayout descriptor_layout;
    vk::helpers::check_vulkan(
        vkCreateDescriptorSetLayout(*device, &layout_info, nullptr, &descriptor_layout));

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_layout;

    VkPipelineLayout pipeline_layout;
    helpers::check_vulkan(
        vkCreatePipelineLayout(*device, &pipeline_layout_info, nullptr, &pipeline_layout));

    VkComputePipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.layout = pipeline_layout;
    pipeline_info.stage = shader_stage_info;

    VkPipeline compute_pipeline;
    helpers::check_vulkan(vkCreateComputePipelines(*device, VK_NULL_HANDLE, 1, &pipeline_info,
                                                   nullptr, &compute_pipeline));

    vkDestroyShaderModule(*device, shader_module, nullptr);

    return std::make_shared<Kernel>(PrivateToken{}, device, descriptor_layout, pipeline_layout,
                                    compute_pipeline, std::move(param_types));
}

void Kernel::invoke(CommandBuffer::ptr cmd_buffer, DescriptorSet args, glm::u64vec3 grid_size) {
    vkCmdBindPipeline(*cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, *this);
    vkCmdBindDescriptorSets(*cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1,
                            &args.descriptor_set, 0, 0);

    vkCmdDispatch(*cmd_buffer, grid_size.x, grid_size.y, grid_size.z);
}

void Kernel::invoke(CommandBuffer::ptr cmd_buffer, DescriptorSet args, size_t grid_size) {
    invoke(cmd_buffer, args, glm::u64vec3(grid_size, 1, 1));
}

}  // namespace spor::vk