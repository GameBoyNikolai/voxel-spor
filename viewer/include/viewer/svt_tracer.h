#pragma once

#include "viewer/vulkan_application.h"
#include "vkh/base_objects.h"
#include "vkh/buffer_objects.h"
#include "vkh/render_objects.h"
#include "vkh/compute.h"
#include "voxel/vdb.h"

namespace spor {

struct TracerUBO {
    glm::mat4 model;

    glm::mat4 view;
    glm::mat4 projection;

    glm::mat4 inv_vp;
    glm::mat4 inv_m;
    glm::vec3 camera_pos;
};

class SvtTracerScene : public Scene {
public:
    SvtTracerScene() = default;
    ~SvtTracerScene() = default;

    SvtTracerScene(SvtTracerScene&&) = default;
    SvtTracerScene& operator=(SvtTracerScene&&) = default;

    SvtTracerScene(const SvtTracerScene&) = delete;
    SvtTracerScene& operator=(const SvtTracerScene&) = delete;

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
    vk::Semaphore::ptr compute_finished_;

    vk::DepthBuffer::ptr depth_buffer_;
    vk::DrawImage::ptr draw_image_;

    vk::CommandPool::ptr cmd_pool_;

    vk::CommandBuffer::ptr cmp_buffer_;

    std::unique_ptr<vox::VDB> vdb_;

    vk::Buffer::ptr tracer_ubo_;
    std::unique_ptr<vk::PersistentMapping<TracerUBO>> tracer_ubo_mapping_;

    vk::Kernel::ptr trace_func_;

    glm::vec2 orbit_rot_{};
    float orbit_radius_ = 15.f;

    vk::Sampler::ptr sampler_;

    std::unique_ptr<vk::DescriptorAllocator> desc_allocator_;

    vk::DescriptorLayout::ptr full_desc_layout_;
    vk::DescriptorSet full_desc_;
};

}  // namespace spor