#include "viewer\test_scene.h"

#include <array>
#include <cstddef>

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "shaders/test.frag.inl"
#include "shaders/test.vert.inl"

namespace spor {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;

    static VkVertexInputBindingDescription binding_description() {
        VkVertexInputBindingDescription desc{};
        desc.binding = 0;
        desc.stride = sizeof(Vertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return desc;
    }

    static std::vector<VkVertexInputAttributeDescription> attribute_descriptions() {
        std::vector<VkVertexInputAttributeDescription> descs(2);

        descs[0].binding = 0;
        descs[0].location = 0;
        descs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        descs[0].offset = offsetof(Vertex, pos);

        descs[1].binding = 0;
        descs[1].location = 1;
        descs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        descs[1].offset = offsetof(Vertex, color);

        return descs;
    }
};

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

void TestScene::setup() {
    mvp_ubo_ = vk::create_uniform_buffer(surface_device_, sizeof(UniformBufferObject), 1);
    mvp_mapping_ = std::make_unique<vk::PersistentMapping>(mvp_ubo_);
    ubo_descriptor_ = vk::DescriptorSetLayout::create(surface_device_, 0);

    graphics_pipeline_ = vk::GraphicsPipelineBuilder(surface_device_, swap_chain_)      //
                             .add_vertex_shader(spor::shaders::test::vert)              //
                             .add_fragment_shader(spor::shaders::test::frag)            //
                             .set_vertex_descriptors(Vertex::binding_description(),     //
                                                     Vertex::attribute_descriptions())  //
                             .add_descriptor_set(ubo_descriptor_)                       //
                             .build();

    framebuffers_
        = vk::SwapChainFramebuffers::create(surface_device_, swap_chain_, graphics_pipeline_);

    cmd_pool_ = vk::CommandPool::create(surface_device_);
    cmd_buffer_ = vk::CommandBuffer::create(surface_device_, cmd_pool_);

    const std::vector<Vertex> vertices = {{{-0.5f, -0.5f, 0.2f}, {1.0f, 0.0f, 0.0f}},
                                          {{0.5f, -0.5f, 0.f}, {0.0f, 1.0f, 0.0f}},
                                          {{0.5f, 0.5f, 0.f}, {0.0f, 0.0f, 1.0f}},
                                          {{-0.5f, 0.5f, 0.2f}, {1.0f, 1.0f, 1.0f}}};
    const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

    vbo_ = vk::create_vertex_buffer(surface_device_, vertices.size(), sizeof(Vertex));
    ibo_ = vk::create_vertex_buffer(surface_device_, indices.size(), sizeof(uint16_t));

    {
        auto transfer_buf = vk::create_and_fill_transfer_buffer(surface_device_, vertices);
        // Note: create a separate CommandPool for transfers using
        // VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
        auto transfer_cmd = vk::buffer_memcpy(surface_device_, cmd_pool_, transfer_buf, vbo_,
                                              transfer_buf->size());
        vk::submit_commands(transfer_cmd, *surface_device_->queues.graphics_queue);
    }
    {
        auto transfer_buf = vk::create_and_fill_transfer_buffer(surface_device_, indices);
        auto transfer_cmd = vk::buffer_memcpy(surface_device_, cmd_pool_, transfer_buf, ibo_,
                                              transfer_buf->size());
        vk::submit_commands(transfer_cmd, *surface_device_->queues.graphics_queue);
    }

    descriptor_pool_ = vk::DescriptorPool::create(surface_device_, 1);
    descriptor_set_ = vk::DescriptorSet::create(surface_device_, descriptor_pool_, ubo_descriptor_);
    vk::update_descriptor_sets(surface_device_, mvp_ubo_, sizeof(UniformBufferObject),
                               descriptor_set_);
}

vk::CommandBuffer::ptr TestScene::render(uint32_t framebuffer_index) {
    update_uniform_buffers();

    vk::record_commands rc(cmd_buffer_);

    VkRect2D view_rect{{0, 0}, swap_chain_->extent};
    vk::render_pass rp(cmd_buffer_, graphics_pipeline_,
                       framebuffers_->framebuffers[framebuffer_index], view_rect);

    vkCmdBindPipeline(cmd_buffer_->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline_->graphics_pipeline);

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

    vkCmdBindDescriptorSets(cmd_buffer_->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            graphics_pipeline_->pipeline_layout, 0, 1,
                            &descriptor_set_->descriptor_set, 0, nullptr);

    VkBuffer vertex_buffers[] = {vbo_->buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd_buffer_->command_buffer, 0, 1, vertex_buffers, offsets);

    vkCmdBindIndexBuffer(cmd_buffer_->command_buffer, ibo_->buffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdDrawIndexed(cmd_buffer_->command_buffer, static_cast<uint32_t>(ibo_->element_count), 1, 0,
                     0, 0);

    return cmd_buffer_;
}

void TestScene::teardown() {}

void TestScene::update_uniform_buffers() {
    time += 0.01f;

    UniformBufferObject ubo{};
    ubo.model
        = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                           glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.projection = glm::perspective(
        glm::radians(45.0f),
        swap_chain_->extent.width / static_cast<float>(swap_chain_->extent.height), 0.1f, 10.0f);

    // Vulkan uses a flipped Projection space Y compared to OGL
    ubo.projection[1][1] *= -1.f;

    // slightly prettier to me than a memcpy?
    // TODO: perhaps make PersistentMapping a template type
    *reinterpret_cast<UniformBufferObject*>(mvp_mapping_->mapped_mem) = ubo;
}

}  // namespace spor