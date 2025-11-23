#pragma once

#include "viewer/model.h"
#include "viewer/vulkan_application.h"
#include "viewer/vulkan_base_objects.h"
#include "viewer/vulkan_buffer_objects.h"
#include "viewer/vulkan_render_objects.h"

namespace spor {

struct MVPUniformBuffer {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

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

    virtual vk::Semaphore::ptr render(uint32_t framebuffer_index, vk::Semaphore::ptr swap_chain_ready) override;

    virtual void teardown() override;

public:
    virtual void block_for_current_frame() override;

public:
    virtual void on_mouse_drag(MouseButton button, glm::vec2 offset) override;

    virtual void on_mouse_scroll(float offset) override;

private:
    void update_uniform_buffers();

private:
    vk::Fence::ptr frame_fence_;
    vk::Semaphore::ptr frame_finished_;

    vk::DepthBuffer::ptr depth_buffer_;
    vk::DrawImage::ptr draw_image_;

    vk::CommandPool::ptr cmd_pool_;

    vk::Model::ptr model_;

    vk::Buffer::ptr mvp_ubo_;
    std::unique_ptr<vk::PersistentMapping<MVPUniformBuffer>> mvp_mapping_;

    glm::vec2 orbit_rot_{};
    float orbit_radius_ = 5.f;

    vk::Sampler::ptr sampler_;

    std::unique_ptr<vk::DescriptorAllocator> desc_allocator_;

    vk::DescriptorLayout::ptr global_desc_layout_;
    vk::DescriptorSet global_desc_;
    vk::DescriptorSet model_desc_;

    vk::GraphicsPipeline::ptr graphics_pipeline_;
};

}  // namespace spor