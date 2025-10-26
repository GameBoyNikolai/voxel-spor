#include "viewer\test_scene.h"

#include "shaders/test.frag.inl"
#include "shaders/test.vert.inl"

namespace spor {
void TestScene::setup() {
    graphics_pipeline_ = vk::GraphicsPipelineBuilder(surface_device_, swap_chain_)  //
                             .add_vertex_shader(spor::shaders::test::vert)          //
                             .add_fragment_shader(spor::shaders::test::frag)        //
                             .build();

    framebuffers_
        = vk::SwapChainFramebuffers::create(surface_device_, swap_chain_, graphics_pipeline_);

    pool_ = vk::CommandPool::create(surface_device_);
    cmd_buffer_ = vk::CommandBuffer::create(surface_device_, pool_);
}

vk::CommandBuffer::ptr TestScene::render(uint32_t framebuffer_index) {
    vk::record_commands rc(cmd_buffer_);

    VkRect2D view_rect{{0, 0}, swap_chain_->extent};
    vk::render_pass rp(cmd_buffer_, graphics_pipeline_,
                       framebuffers_->framebuffers[framebuffer_index], view_rect);

    vkCmdBindPipeline(cmd_buffer_->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_->graphics_pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(view_rect.extent.width);
    viewport.height = static_cast<float>(view_rect.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buffer_->command_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swap_chain_->extent;
    vkCmdSetScissor(cmd_buffer_->command_buffer, 0, 1, &scissor);

    vkCmdDraw(cmd_buffer_->command_buffer, 3, 1, 0, 0);

    //vkResetCommandBuffer(cmd_buffer_->command_buffer, 0);

    //VkCommandBufferBeginInfo begin_info{};
    //begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    //begin_info.flags = 0;                   // Optional
    //begin_info.pInheritanceInfo = nullptr;  // Optional

    //vkBeginCommandBuffer(cmd_buffer_->command_buffer, &begin_info);

    //VkRenderPassBeginInfo render_pass_info{};
    //render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    //render_pass_info.renderPass = graphics_pipeline_->render_pass;
    //render_pass_info.framebuffer = framebuffers_->framebuffers[framebuffer_index];

    //render_pass_info.renderArea.offset = {0, 0};
    //render_pass_info.renderArea.extent = swap_chain_->extent;

    //VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    //render_pass_info.clearValueCount = 1;
    //render_pass_info.pClearValues = &clear_color;

    //vkCmdBeginRenderPass(cmd_buffer_->command_buffer, &render_pass_info,
    //                     VK_SUBPASS_CONTENTS_INLINE);

    //vkCmdBindPipeline(cmd_buffer_->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
    //                  graphics_pipeline_->graphics_pipeline);

    //VkViewport viewport{};
    //viewport.x = 0.0f;
    //viewport.y = 0.0f;
    //viewport.width = static_cast<float>(swap_chain_->extent.width);
    //viewport.height = static_cast<float>(swap_chain_->extent.height);
    //viewport.minDepth = 0.0f;
    //viewport.maxDepth = 1.0f;
    //vkCmdSetViewport(cmd_buffer_->command_buffer, 0, 1, &viewport);

    //VkRect2D scissor{};
    //scissor.offset = {0, 0};
    //scissor.extent = swap_chain_->extent;
    //vkCmdSetScissor(cmd_buffer_->command_buffer, 0, 1, &scissor);

    //vkCmdDraw(cmd_buffer_->command_buffer, 3, 1, 0, 0);

    //vkCmdEndRenderPass(cmd_buffer_->command_buffer);

    //vkEndCommandBuffer(cmd_buffer_->command_buffer);

    return cmd_buffer_;
}

void TestScene::teardown() {}

}  // namespace spor