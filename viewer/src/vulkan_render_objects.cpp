#include "viewer/vulkan_render_objects.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>

namespace spor::vk {

namespace {
VkDescriptorType to_desc_type(DescParameter::ParamType type) {
    switch (type) {
        case DescParameter::ParamType::kUBO:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case DescParameter::ParamType::kSSBO:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case DescParameter::ParamType::kSampledImage:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case DescParameter::ParamType::kStorageImage:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }

    throw std::invalid_argument("Invalid DescParameter ParamType");
}
}  // namespace

RenderPass::~RenderPass() { vkDestroyRenderPass(*device_, render_pass, nullptr); }

RenderPass::ptr RenderPass::create(SurfaceDevice::ptr device, VkFormat color_format,
                                   std::optional<VkFormat> depth_stencil_format) {
    auto render_pass = helpers::create_render_pass(*device, color_format, depth_stencil_format);

    return std::make_shared<RenderPass>(PrivateToken{}, device, render_pass, color_format,
                                        depth_stencil_format);
}

GraphicsPipeline::~GraphicsPipeline() {
    vkDestroyPipeline(*surface_device_, graphics_pipeline, nullptr);
    vkDestroyPipelineLayout(*surface_device_, pipeline_layout, nullptr);
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::add_vertex_shader(
    const std::vector<uint32_t>& shader) {
    add_shader(VK_SHADER_STAGE_VERTEX_BIT, shader.data(), shader.size());

    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::add_fragment_shader(
    const std::vector<uint32_t>& shader) {
    add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, shader.data(), shader.size());

    return *this;
}

void GraphicsPipelineBuilder::add_shader(VkShaderStageFlagBits stage, const uint32_t* shader_data,
                                         size_t len) {
    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = len * sizeof(uint32_t);
    create_info.pCode = shader_data;

    VkShaderModule shader_module;
    helpers::check_vulkan(
        vkCreateShaderModule(*surface_device_, &create_info, nullptr, &shader_module));
    shaders_.push_back(shader_module);

    auto& shader_stage_info = shader_stages_.emplace_back();
    shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage_info.stage = stage;

    shader_stage_info.module = shader_module;
    shader_stage_info.pName = "main";
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::set_vertex_descriptors(
    VkVertexInputBindingDescription binding_desc,
    std::vector<VkVertexInputAttributeDescription> attrib_descs) {
    vertex_descriptors_ = VertexDescriptors{binding_desc, std::move(attrib_descs)};

    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::set_primitive_type(
    VkPrimitiveTopology primitive_type) {
    primitive_type_ = primitive_type;
    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::add_global_layout(DescriptorLayout::ptr layout) {
    descriptor_layouts_.push_back(layout);

    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::add_local_layout(
    const std::vector<DescParameter>& params) {
    descriptor_layouts_.push_back(DescriptorLayout::create(surface_device_, params));

    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::enable_depth_testing() {
    depth_testing_ = true;
    return *this;
}

GraphicsPipeline::ptr GraphicsPipelineBuilder::build() {
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 0;
    vertex_input.pVertexBindingDescriptions = nullptr;
    vertex_input.vertexAttributeDescriptionCount = 0;
    vertex_input.pVertexAttributeDescriptions = nullptr;

    if (vertex_descriptors_) {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &vertex_descriptors_->binding_desc;

        vertex_input.vertexAttributeDescriptionCount
            = static_cast<uint32_t>(vertex_descriptors_->attrib_descs.size());
        vertex_input.pVertexAttributeDescriptions = vertex_descriptors_->attrib_descs.data();
    }

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = primitive_type_;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    std::vector<VkDynamicState> dynamic_states
        = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state_info{};
    dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state_info.pDynamicStates = dynamic_states.data();

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swap_chain_->extent.width);
    viewport.height = static_cast<float>(swap_chain_->extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swap_chain_->extent;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;  // Optional
    rasterizer.depthBiasClamp = 0.0f;           // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f;     // Optional

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampling.minSampleShading = 1.0f;           // Optional
    multisampling.pSampleMask = nullptr;             // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE;  // Optional
    multisampling.alphaToOneEnable = VK_FALSE;       // Optional

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY;  // Optional
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;
    color_blending.blendConstants[0] = 0.0f;  // Optional
    color_blending.blendConstants[1] = 0.0f;  // Optional
    color_blending.blendConstants[2] = 0.0f;  // Optional
    color_blending.blendConstants[3] = 0.0f;  // Optional

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    if (depth_testing_) {
        depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_stencil.depthTestEnable = VK_TRUE;
        depth_stencil.depthWriteEnable = VK_TRUE;
        depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depth_stencil.depthBoundsTestEnable = VK_FALSE;
        depth_stencil.minDepthBounds = 0.0f;  // Optional
        depth_stencil.maxDepthBounds = 1.0f;  // Optional
        depth_stencil.stencilTestEnable = VK_FALSE;
        depth_stencil.front = {};  // Optional
        depth_stencil.back = {};   // Optional
    }

    std::vector<VkDescriptorSetLayout> set_layouts;
    set_layouts.reserve(descriptor_layouts_.size());
    std::transform(descriptor_layouts_.begin(), descriptor_layouts_.end(),
                   std::back_inserter(set_layouts),
                   [](const auto& layout) { return layout->layout; });

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    pipeline_layout_info.setLayoutCount = static_cast<uint32_t>(set_layouts.size());
    pipeline_layout_info.pSetLayouts = set_layouts.data();

    pipeline_layout_info.pushConstantRangeCount = 0;     // Optional
    pipeline_layout_info.pPushConstantRanges = nullptr;  // Optional

    VkPipelineLayout layout;
    helpers::check_vulkan(
        vkCreatePipelineLayout(*surface_device_, &pipeline_layout_info, nullptr, &layout));

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = static_cast<uint32_t>(shader_stages_.size());
    pipeline_info.pStages = shader_stages_.data();

    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = depth_testing_ ? &depth_stencil : nullptr;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state_info;

    pipeline_info.layout = layout;

    if (render_pass_) {
        pipeline_info.renderPass = *render_pass_;
    }
    pipeline_info.subpass = 0;

    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;  // Optional
    pipeline_info.basePipelineIndex = -1;               // Optional

    VkPipeline pipeline;
    helpers::check_vulkan(vkCreateGraphicsPipelines(*surface_device_, VK_NULL_HANDLE, 1,
                                                    &pipeline_info, nullptr, &pipeline));

    for (auto& shader_module : shaders_) {
        vkDestroyShaderModule(*surface_device_, shader_module, nullptr);
    }

    shaders_.clear();
    shader_stages_.clear();

    return std::make_shared<GraphicsPipeline>(GraphicsPipeline::PrivateToken{}, surface_device_,
                                              swap_chain_, render_pass_, layout, pipeline,
                                              std::move(descriptor_layouts_));
}

DepthBuffer::~DepthBuffer() {
    vkDestroyImageView(*surface_device_, view, nullptr);
    vkDestroyImage(*surface_device_, image, nullptr);
    vkFreeMemory(*surface_device_, memory, nullptr);
}

VkFormat DepthBuffer::default_format(SurfaceDevice::ptr surface_device) {
    return helpers::choose_supported_format(
        *surface_device,
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

DepthBuffer::ptr DepthBuffer::create(SurfaceDevice::ptr surface_device, size_t w, size_t h,
                                     VkFormat format) {
    auto [image, memory] = helpers::create_image(
        surface_device->device, surface_device->physical_device, w, h, format,
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    auto view
        = helpers::create_image_view(*surface_device, image, format, VK_IMAGE_ASPECT_DEPTH_BIT);

    return std::make_shared<DepthBuffer>(PrivateToken{}, surface_device, image, view, memory, w, h);
}

DepthBuffer::ptr DepthBuffer::create(SurfaceDevice::ptr surface_device, size_t w, size_t h) {
    return create(surface_device, w, h, default_format(surface_device));
}

SwapChainFramebuffers::~SwapChainFramebuffers() {
    for (const auto& framebuffer : framebuffers) {
        vkDestroyFramebuffer(*surface_device_, framebuffer, nullptr);
    }
}

SwapChainFramebuffers::ptr SwapChainFramebuffers::create(SurfaceDevice::ptr surface_device,
                                                         SwapChain::ptr swap_chain,
                                                         RenderPass::ptr render_pass) {
    DepthBuffer::ptr depth_buffer;
    if (render_pass->depth_stencil_format) {
        depth_buffer = DepthBuffer::create(surface_device, swap_chain->extent.width,
                                           swap_chain->extent.height);
    }

    std::vector<VkFramebuffer> framebuffers(swap_chain->swap_chain_views.size());

    for (size_t i = 0; i < swap_chain->swap_chain_views.size(); i++) {
        std::vector<VkImageView> attachments = {swap_chain->swap_chain_views[i]};
        if (depth_buffer) {
            attachments.push_back(depth_buffer->view);
        }

        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = *render_pass;
        framebuffer_info.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebuffer_info.pAttachments = attachments.data();
        framebuffer_info.width = swap_chain->extent.width;
        framebuffer_info.height = swap_chain->extent.height;
        framebuffer_info.layers = 1;

        helpers::check_vulkan(
            vkCreateFramebuffer(*surface_device, &framebuffer_info, nullptr, &framebuffers[i]));
    }

    return std::make_shared<SwapChainFramebuffers>(PrivateToken{}, surface_device, swap_chain,
                                                   render_pass, std::move(framebuffers),
                                                   depth_buffer);
}

begin_render_pass::begin_render_pass(CommandBuffer::ptr command_buffer, RenderPass::ptr render_pass,
                                     VkFramebuffer framebuffer, VkRect2D area)
    : command_buffer_(command_buffer) {
    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = *render_pass;
    render_pass_info.framebuffer = framebuffer;

    render_pass_info.renderArea = area;

    std::array<VkClearValue, 2> clear_values{};
    clear_values[0].color = {0.f, 0.f, 0.f, 1.0f};
    clear_values[1].depthStencil = {1.0f, 0};

    render_pass_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
    render_pass_info.pClearValues = clear_values.data();

    vkCmdBeginRenderPass(command_buffer_->command_buffer, &render_pass_info,
                         VK_SUBPASS_CONTENTS_INLINE);
}

begin_render_pass::~begin_render_pass() {
    if (command_buffer_) {
        vkCmdEndRenderPass(command_buffer_->command_buffer);
    }
}

begin_render_pass::begin_render_pass(begin_render_pass&& other) noexcept {
    *this = std::move(other);
}

begin_render_pass& begin_render_pass::operator=(begin_render_pass&& other) noexcept {
    std::swap(command_buffer_, other.command_buffer_);

    return *this;
}

start_rendering::start_rendering(CommandBuffer::ptr command_buffer, VkRect2D area,
                                 const helpers::ImageView& color_attachment,
                                 const helpers::ImageView& depth_attachment,
                                 const glm::vec4& clear_color)
    : command_buffer_(command_buffer) {
    VkRenderingAttachmentInfo color_attachment_info{};
    color_attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attachment_info.pNext = nullptr;

    color_attachment_info.imageView = color_attachment.view;
    color_attachment_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment_info.clearValue.color
        = {clear_color.x, clear_color.y, clear_color.z, clear_color.w};

    VkRenderingAttachmentInfo depth_attachment_info{};
    depth_attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth_attachment_info.pNext = nullptr;

    depth_attachment_info.imageView = depth_attachment.view;
    depth_attachment_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_attachment_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment_info.clearValue.depthStencil = {1.f, 0};

    VkRenderingInfo render_info{};
    render_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    render_info.pNext = nullptr;

    render_info.renderArea = area;
    render_info.layerCount = 1;
    render_info.colorAttachmentCount = 1;
    render_info.pColorAttachments = &color_attachment_info;
    render_info.pDepthAttachment = &depth_attachment_info;
    render_info.pStencilAttachment = &depth_attachment_info;

    vkCmdBeginRendering(*command_buffer_, &render_info);
}

start_rendering::~start_rendering() {
    if (command_buffer_) {
        vkCmdEndRenderPass(command_buffer_->command_buffer);
    }
}

start_rendering::start_rendering(start_rendering&& other) noexcept { *this = std::move(other); }

start_rendering& start_rendering::operator=(start_rendering&& other) noexcept {
    std::swap(command_buffer_, other.command_buffer_);

    return *this;
}

DefaultRenderSyncObjects::~DefaultRenderSyncObjects() {
    vkDestroyFence(*surface_device_, in_flight, nullptr);
    vkDestroySemaphore(*surface_device_, render_finished, nullptr);
    vkDestroySemaphore(*surface_device_, image_available, nullptr);
}

DefaultRenderSyncObjects::ptr DefaultRenderSyncObjects::create(vk::SurfaceDevice::ptr device) {
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;
    helpers::check_vulkan(
        vkCreateSemaphore(device->device, &semaphore_info, nullptr, &image_available));
    helpers::check_vulkan(
        vkCreateSemaphore(device->device, &semaphore_info, nullptr, &render_finished));
    helpers::check_vulkan(vkCreateFence(device->device, &fence_info, nullptr, &in_flight));

    return std::make_shared<DefaultRenderSyncObjects>(PrivateToken{}, device, image_available,
                                                      render_finished, in_flight);
}

Fence::~Fence() { vkDestroyFence(*surface_device_, fence, nullptr); }

Fence::ptr Fence::create(vk::SurfaceDevice::ptr device) {
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkFence fence;
    helpers::check_vulkan(vkCreateFence(device->device, &fence_info, nullptr, &fence));

    return std::make_shared<Fence>(PrivateToken{}, device, fence);
}

Semaphore::~Semaphore() { vkDestroySemaphore(*surface_device_, semaphore, nullptr); }

Semaphore::ptr Semaphore::create(vk::SurfaceDevice::ptr device) {
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkSemaphore semaphore;
    helpers::check_vulkan(vkCreateSemaphore(device->device, &semaphore_info, nullptr, &semaphore));

    return std::make_shared<Semaphore>(PrivateToken{}, device, semaphore);
}

DescriptorUpdater::DescriptorUpdater(SurfaceDevice::ptr device, VkDescriptorSet desc)
    : device_(device), desc_to_update_(desc) {}

DescriptorUpdater& DescriptorUpdater::with_ubo(uint32_t binding, Buffer::ptr buffer,
                                               std::optional<size_t> offset,
                                               std::optional<size_t> size) {
    auto& desc_write = add_write(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    desc_write.dstBinding = binding;

    auto& buffer_info = buffer_infos_.emplace_back();
    buffer_info.buffer = buffer->buffer;
    buffer_info.offset = offset.value_or(0);
    buffer_info.range = offset.value_or(buffer->size());

    desc_write.pBufferInfo = &buffer_info;

    return *this;
}

DescriptorUpdater& DescriptorUpdater::with_ssbo(uint32_t binding, Buffer::ptr buffer,
                                                std::optional<size_t> offset,
                                                std::optional<size_t> size) {
    auto& desc_write = add_write(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    desc_write.dstBinding = binding;

    auto& buffer_info = buffer_infos_.emplace_back();
    buffer_info.buffer = buffer->buffer;
    buffer_info.offset = offset.value_or(0);
    buffer_info.range = offset.value_or(buffer->size());

    desc_write.pBufferInfo = &buffer_info;

    return *this;
}

DescriptorUpdater& DescriptorUpdater::with_sampled_image(uint32_t binding, Texture::ptr texture,
                                                         vk::Sampler::ptr sampler,
                                                         std::optional<VkImageLayout> layout) {
    auto& desc_write = add_write(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    desc_write.dstBinding = binding;

    auto& image_info = image_infos_.emplace_back();
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.imageView = texture->view;
    image_info.sampler = *sampler;

    desc_write.pImageInfo = &image_info;

    return *this;
}

DescriptorUpdater& DescriptorUpdater::with_storage_image(uint32_t binding, Texture::ptr texture,
                                                         std::optional<VkImageLayout> layout) {
    auto& desc_write = add_write(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    desc_write.dstBinding = binding;

    auto& image_info = image_infos_.emplace_back();
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.imageView = texture->view;

    desc_write.pImageInfo = &image_info;

    return *this;
}

DescriptorSet DescriptorUpdater::update() {
    std::set<uint32_t> used_bindings;
    for (auto& write : writes_) {
        used_bindings.insert(write.dstBinding);
    }

    if (used_bindings.size() != writes_.size()) {
        throw std::invalid_argument("Repeated binding point used");
    }

    vkUpdateDescriptorSets(*device_, static_cast<uint32_t>(writes_.size()), writes_.data(), 0,
                           nullptr);

    return DescriptorSet{desc_to_update_};
}

DescriptorSet DescriptorUpdater::get() { return DescriptorSet{desc_to_update_}; }

VkWriteDescriptorSet& DescriptorUpdater::add_write(VkDescriptorType type) {
    auto& descriptor_write = writes_.emplace_back();
    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

    descriptor_write.dstSet = desc_to_update_;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorCount = 1;
    descriptor_write.descriptorType = type;

    descriptor_write.pBufferInfo = nullptr;
    descriptor_write.pImageInfo = nullptr;
    descriptor_write.pTexelBufferView = nullptr;

    return descriptor_write;
}

DescriptorAllocator::DescriptorAllocator(SurfaceDevice::ptr device, size_t set_size,
                                         std::vector<PoolSizeRatio> ratios)
    : device_(device), set_size_(set_size), ratios_(std::move(ratios)) {}

DescriptorAllocator::~DescriptorAllocator() {
    for (auto pool : active_pools_) {
        vkDestroyDescriptorPool(*device_, pool, nullptr);
    }

    for (auto pool : full_pools_) {
        vkDestroyDescriptorPool(*device_, pool, nullptr);
    }
}

DescriptorAllocator::DescriptorAllocator(DescriptorAllocator&& other) noexcept {
    *this = std::move(other);
}

DescriptorAllocator& DescriptorAllocator::operator=(DescriptorAllocator&& other) noexcept {
    std::swap(device_, other.device_);
    std::swap(ratios_, other.ratios_);
    std::swap(set_size_, other.set_size_);
    std::swap(full_pools_, other.full_pools_);
    std::swap(active_pools_, other.active_pools_);

    return *this;
}

DescriptorUpdater DescriptorAllocator::allocate(VkDescriptorSetLayout layout) {
    VkDescriptorPool pool = next_pool();

    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &layout;

    VkDescriptorSet desc;
    VkResult result = vkAllocateDescriptorSets(*device_, &alloc_info, &desc);

    // get a fresh pool on allocation failure
    if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
        full_pools_.push_back(pool);

        pool = next_pool();
        alloc_info.descriptorPool = pool;

        helpers::check_vulkan(vkAllocateDescriptorSets(*device_, &alloc_info, &desc));
    } else {
        helpers::check_vulkan(result);
    }

    active_pools_.push_back(pool);

    return DescriptorUpdater(device_, desc);
}

void DescriptorAllocator::clear() {
    for (auto pool : active_pools_) {
        helpers::check_vulkan(vkResetDescriptorPool(*device_, pool, 0));
    }

    for (auto pool : full_pools_) {
        helpers::check_vulkan(vkResetDescriptorPool(*device_, pool, 0));
    }

    active_pools_.insert(active_pools_.end(), full_pools_.begin(), full_pools_.end());
    full_pools_.clear();
}

VkDescriptorPool DescriptorAllocator::next_pool() {
    VkDescriptorPool pool;
    if (!active_pools_.empty()) {
        pool = active_pools_.back();
        active_pools_.pop_back();
    } else {
        std::vector<VkDescriptorPoolSize> pool_sizes;
        for (PoolSizeRatio ratio : ratios_) {
            pool_sizes.push_back({ratio.type, static_cast<uint32_t>(ratio.ratio * set_size_)});
        }

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = 0;
        pool_info.maxSets = static_cast<uint32_t>(set_size_);
        pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();

        vkCreateDescriptorPool(*device_, &pool_info, nullptr, &pool);
    }

    return pool;
}

DescriptorLayout::~DescriptorLayout() { vkDestroyDescriptorSetLayout(*device_, layout, nullptr); }

DescriptorLayout::ptr DescriptorLayout::create(SurfaceDevice::ptr device,
                                               const std::vector<DescParameter>& params) {
    std::vector<VkDescriptorSetLayoutBinding> set_layouts;

    for (const auto& param : params) {
        auto param_desc = to_desc_type(param.type);

        auto& layout = set_layouts.emplace_back();
        layout.binding = param.binding;
        layout.descriptorType = param_desc;
        layout.descriptorCount = 1;
        layout.stageFlags = param.shader_stages;
        layout.pImmutableSamplers = nullptr;  // Optional
    }

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(set_layouts.size());
    layout_info.pBindings = set_layouts.data();

    VkDescriptorSetLayout descriptor_layout;
    vk::helpers::check_vulkan(
        vkCreateDescriptorSetLayout(*device, &layout_info, nullptr, &descriptor_layout));

    return std::make_shared<DescriptorLayout>(PrivateToken{}, device, descriptor_layout);
}

}  // namespace spor::vk