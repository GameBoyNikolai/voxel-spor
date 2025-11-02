#include "viewer/vulkan_render_objects.h"

#include <algorithm>

namespace spor::vk {

GraphicsPipeline::~GraphicsPipeline() {}

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

GraphicsPipelineBuilder& GraphicsPipelineBuilder::add_descriptor_set(
    VkDescriptorSetLayout descriptor_set) {
    descriptor_sets_.push_back(descriptor_set);
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
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    pipeline_layout_info.setLayoutCount = static_cast<uint32_t>(descriptor_sets_.size());
    pipeline_layout_info.pSetLayouts = descriptor_sets_.data();

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
    pipeline_info.pDepthStencilState = nullptr;  // Optional
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state_info;

    pipeline_info.layout = layout;

    auto render_pass = helpers::create_render_pass(*surface_device_, swap_chain_->format);
    pipeline_info.renderPass = render_pass;
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
    descriptor_sets_.clear();

    return std::make_shared<GraphicsPipeline>(GraphicsPipeline::PrivateToken{}, surface_device_,
                                              swap_chain_, render_pass, layout, pipeline);
}

CommandPool::~CommandPool() {
    vkDestroyCommandPool(*surface_device_, command_pool, nullptr);
}

CommandPool::ptr CommandPool::create(SurfaceDevice::ptr surface_device) {
    helpers::VulkanQueueIndices queue_indices = surface_device->indices;

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    // guaranteed to be present, but worth checking
    pool_info.queueFamilyIndex = queue_indices.graphics_family.value();

    VkCommandPool pool;
    helpers::check_vulkan(vkCreateCommandPool(*surface_device, &pool_info, nullptr, &pool));

    return std::make_shared<CommandPool>(PrivateToken{}, surface_device, pool);
}

CommandBuffer::ptr CommandBuffer::create(SurfaceDevice::ptr surface_device,
                                         CommandPool::ptr command_pool) {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool->command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer buffer;
    helpers::check_vulkan(vkAllocateCommandBuffers(*surface_device, &alloc_info, &buffer));

    return std::make_shared<CommandBuffer>(PrivateToken{}, surface_device, buffer);
}

SwapChainFramebuffers::~SwapChainFramebuffers() {}  // TODO: destroy framebuffers

SwapChainFramebuffers::ptr SwapChainFramebuffers::create(SurfaceDevice::ptr surface_device,
                                                         SwapChain::ptr swap_chain,
                                                         GraphicsPipeline::ptr pipeline) {
    std::vector<VkFramebuffer> framebuffers(swap_chain->swap_chain_views.size());

    for (size_t i = 0; i < swap_chain->swap_chain_views.size(); i++) {
        VkImageView attachments[] = {swap_chain->swap_chain_views[i]};

        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = pipeline->render_pass;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = swap_chain->extent.width;
        framebuffer_info.height = swap_chain->extent.height;
        framebuffer_info.layers = 1;

        helpers::check_vulkan(vkCreateFramebuffer(*surface_device, &framebuffer_info,
                                                  nullptr, &framebuffers[i]));
    }

    return std::make_shared<SwapChainFramebuffers>(PrivateToken{}, surface_device, swap_chain,
                                                   pipeline, std::move(framebuffers));
}

record_commands::record_commands(CommandBuffer::ptr command_buffer)
    : command_buffer_(command_buffer) {
    helpers::check_vulkan(vkResetCommandBuffer(command_buffer->command_buffer, 0));

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = 0;                   // Optional
    begin_info.pInheritanceInfo = nullptr;  // Optional

    helpers::check_vulkan(vkBeginCommandBuffer(command_buffer->command_buffer, &begin_info));
}

record_commands::~record_commands() {
    if (command_buffer_) {
        helpers::check_vulkan(vkEndCommandBuffer(command_buffer_->command_buffer));
    }
}

record_commands::record_commands(record_commands&& other) noexcept { *this = std::move(other); }

record_commands& record_commands::operator=(record_commands&& other) noexcept {
    std::swap(command_buffer_, other.command_buffer_);

    return *this;
}

render_pass::render_pass(CommandBuffer::ptr command_buffer, GraphicsPipeline::ptr pipeline,
                         VkFramebuffer framebuffer, VkRect2D area)
    : command_buffer_(command_buffer) {
    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = pipeline->render_pass;
    render_pass_info.framebuffer = framebuffer;

    render_pass_info.renderArea = area;

    VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass(command_buffer_->command_buffer, &render_pass_info,
                         VK_SUBPASS_CONTENTS_INLINE);
}

render_pass::~render_pass() {
    if (command_buffer_) {
        vkCmdEndRenderPass(command_buffer_->command_buffer);
    }
}

render_pass::render_pass(render_pass&& other) noexcept { *this = std::move(other); }

render_pass& render_pass::operator=(render_pass&& other) noexcept {
    std::swap(command_buffer_, other.command_buffer_);

    return *this;
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

DefaultRenderSyncObjects::~DefaultRenderSyncObjects() {
    vkDestroyFence(*surface_device_, in_flight, nullptr);
    vkDestroySemaphore(*surface_device_, render_finished, nullptr);
    vkDestroySemaphore(*surface_device_, image_available, nullptr);
}

}  // namespace spor::vk