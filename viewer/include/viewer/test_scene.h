#pragma once

#include "viewer/buffer_objects.h"
#include "viewer/vulkan_application.h"
#include "viewer/vulkan_objects.h"

namespace spor {

class TestScene : public Scene {
public:
    TestScene() = default;
    ~TestScene() = default;

    TestScene(TestScene&&) = default;
    TestScene& operator=(TestScene&&) = default;

    TestScene(const TestScene&) = delete;
    TestScene& operator=(const TestScene&) = delete;

public:
    virtual void setup();

    virtual vk::CommandBuffer::ptr render(uint32_t framebuffer_index);

    virtual void teardown();

private:
    void update_uniform_buffers();

private:
    vk::GraphicsPipeline::ptr graphics_pipeline_;
    vk::SwapChainFramebuffers::ptr framebuffers_;

    vk::CommandPool::ptr cmd_pool_;
    vk::CommandBuffer::ptr cmd_buffer_;

    vk::Buffer::ptr vbo_;
    vk::Buffer::ptr ibo_;

    vk::Buffer::ptr mvp_ubo_;
    std::unique_ptr<vk::PersistentMapping> mvp_mapping_;
    vk::DescriptorSetLayout::ptr ubo_descriptor_;

    vk::DescriptorPool::ptr descriptor_pool_;
    vk::DescriptorSet::ptr descriptor_set_;

    float time = 0.f;
};

}  // namespace spor