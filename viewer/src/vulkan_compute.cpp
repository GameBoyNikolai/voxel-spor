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

    std::map<VkDescriptorType, size_t> type_indices;
    std::vector<VkDescriptorPoolSize> pool_sizes;

    std::vector<VkDescriptorSetLayoutBinding> set_layouts;

    for (const auto& param : param_types) {
        auto param_desc = to_desc_type(param);

        if (type_indices.count(param_desc)) {
            ++pool_sizes[type_indices[param_desc]].descriptorCount;
        } else {
            pool_sizes.push_back({param_desc, 1});
            type_indices[param_desc] = pool_sizes.size() - 1;
        }

        auto& layout = set_layouts.emplace_back();
        layout.binding = static_cast<uint32_t>(set_layouts.size() - 1);
        layout.descriptorType = param_desc;
        layout.descriptorCount = 1;
        layout.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        layout.pImmutableSamplers = nullptr;  // Optional
    }

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    pool_info.maxSets = static_cast<uint32_t>(param_types.size());

    VkDescriptorPool pool;
    helpers::check_vulkan(vkCreateDescriptorPool(*device, &pool_info, nullptr, &pool));

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(set_layouts.size());
    layout_info.pBindings = set_layouts.data();

    VkDescriptorSetLayout descriptor_layout;
    vk::helpers::check_vulkan(
        vkCreateDescriptorSetLayout(*device, &layout_info, nullptr, &descriptor_layout));

    std::vector<VkDescriptorSetLayout> layouts(1, descriptor_layout);
    VkDescriptorSetAllocateInfo set_alloc_info{};
    set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_alloc_info.descriptorPool = pool;
    set_alloc_info.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    set_alloc_info.pSetLayouts = layouts.data();

    VkDescriptorSet descriptor_set;
    helpers::check_vulkan(vkAllocateDescriptorSets(*device, &set_alloc_info, &descriptor_set));

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

    return std::make_shared<Kernel>(PrivateToken{}, device, pool, descriptor_layout, descriptor_set,
                                    pipeline_layout, compute_pipeline, std::move(param_types));
}
Invoker::Invoker(Kernel::ptr kernel) : kernel_(kernel) {}

Invoker::~Invoker() = default;

Invoker::Invoker(Invoker&& other) noexcept { *this = std::move(other); }

Invoker& Invoker::operator=(Invoker&& other) noexcept {
    std::swap(kernel_, other.kernel_);
    std::swap(args_, other.args_);

    return *this;
}

Invoker& Invoker::with_ubo(Buffer::ptr buffer, std::optional<size_t> offset,
                           std::optional<size_t> size) {
    args_.push_back(BufferObject{buffer, offset.value_or(0), size.value_or(buffer->size())});

    return *this;
}

Invoker& Invoker::with_ssbo(Buffer::ptr buffer, std::optional<size_t> offset,
                            std::optional<size_t> size) {
    args_.push_back(BufferObject{buffer, offset.value_or(0), size.value_or(buffer->size())});

    return *this;
}

Invoker& Invoker::with_storage_image(Texture::ptr texture, Sampler::ptr sampler,
                                     std::optional<VkImageLayout> layout) {
    args_.push_back(StorageImage{texture, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});

    return *this;
}

void Invoker::invoke(CommandBuffer::ptr cmd_buffer, glm::u64vec3 grid_size) {
    if (args_.size() != kernel_->parameters_.size()) {
        throw std::invalid_argument("Argument list does not match the size of the parameter list");
    }

    // Do descriptor updates
    std::vector<VkWriteDescriptorSet> set_writes;

    // we will store pointers to these elements, but by reserving the maximum size for each, we
    // *won't* need to worry about reallocs moving them
    std::vector<VkDescriptorBufferInfo> descriptor_buffer_infos;
    descriptor_buffer_infos.reserve(args_.size());

    std::vector<VkDescriptorImageInfo> descriptor_image_infos;
    descriptor_image_infos.reserve(args_.size());

    for (size_t arg_index = 0; arg_index < args_.size(); ++arg_index) {
        const auto& arg = args_[arg_index];

        auto& descriptor_write = set_writes.emplace_back();
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstSet = kernel_->descriptor_set_;
        descriptor_write.dstBinding = static_cast<uint32_t>(
            arg_index);  // meant to mirror the descriptor set layout bindings from the Kernel, but
                         // we can relatively safely just count up from 0
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorCount = 1;
        descriptor_write.descriptorType = to_desc_type(kernel_->parameters_[arg_index]);

        descriptor_write.pBufferInfo = nullptr;
        descriptor_write.pImageInfo = nullptr;
        descriptor_write.pTexelBufferView = nullptr;

        if (const auto* buffer = std::get_if<BufferObject>(&arg)) {
            auto& buffer_info = descriptor_buffer_infos.emplace_back();
            buffer_info.buffer = buffer->buffer->buffer;  // :)
            buffer_info.offset = buffer->offset;
            buffer_info.range = buffer->size;

            descriptor_write.pBufferInfo = &buffer_info;
        } else if (const auto* img = std::get_if<StorageImage>(&arg)) {
            auto& image_info = descriptor_image_infos.emplace_back();
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            image_info.imageView = img->texture->view;
            image_info.sampler = img->sampler->sampler;

            descriptor_write.pImageInfo = &image_info;
        } else {
            // remove the added write?
        }
    }

    vkUpdateDescriptorSets(*kernel_->device_, static_cast<uint32_t>(set_writes.size()),
                           set_writes.data(), 0, nullptr);

    // invoke the kernel on this command buffer

    auto rc = record_commands(cmd_buffer, false);

    vkCmdBindPipeline(*cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, *kernel_);
    vkCmdBindDescriptorSets(*cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, kernel_->pipeline_layout, 0, 1, &kernel_->descriptor_set_, 0, 0);

    vkCmdDispatch(*cmd_buffer, grid_size.x, grid_size.y, grid_size.z);
}

void Invoker::invoke(CommandBuffer::ptr cmd_buffer, size_t grid_size) {
    invoke(cmd_buffer, glm::u64vec3(grid_size, 1, 1));
}

}  // namespace spor::vk