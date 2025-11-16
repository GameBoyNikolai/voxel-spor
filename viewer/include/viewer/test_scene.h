#pragma once

#include "viewer/vulkan_buffer_objects.h"
#include "viewer/vulkan_application.h"
#include "viewer/vulkan_base_objects.h"
#include "viewer/vulkan_render_objects.h"
#include "viewer/model.h"

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
    virtual void setup() override;

    virtual vk::CommandBuffer::ptr render(uint32_t framebuffer_index) override;

    virtual void teardown() override;

public:
    virtual void on_mouse_drag(MouseButton button, glm::vec2 offset) override;

    virtual void on_mouse_scroll(float offset) override;

private:
    void update_uniform_buffers();

private:
    vk::CommandPool::ptr cmd_pool_;
    vk::CommandBuffer::ptr cmd_buffer_;

    vk::Model::ptr model_;

    //vk::Buffer::ptr vbo_;
    //vk::Buffer::ptr ibo_;

    vk::Buffer::ptr mvp_ubo_;
    std::unique_ptr<vk::PersistentMapping> mvp_mapping_;

    glm::vec2 orbit_rot_{};
    float orbit_radius_ = 5.f;

    //vk::Texture::ptr texture_;
    vk::Sampler::ptr sampler_;

    vk::PipelineDescriptors::ptr descriptors_;

    vk::RenderPass::ptr render_pass_;

    vk::GraphicsPipeline::ptr graphics_pipeline_;
    vk::SwapChainFramebuffers::ptr framebuffers_;
};

}  // namespace spor