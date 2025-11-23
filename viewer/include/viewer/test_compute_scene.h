#pragma once

#include <array>

#include "viewer/model.h"
#include "viewer/vulkan_application.h"
#include "viewer/vulkan_base_objects.h"
#include "viewer/vulkan_buffer_objects.h"
#include "viewer/vulkan_compute.h"
#include "viewer/vulkan_render_objects.h"

namespace spor {

class KernelUBO;

class TestComputeScene : public Scene {
    static constexpr size_t kNumParticles = 1'000'000;

public:
    TestComputeScene() = default;
    ~TestComputeScene() = default;

    TestComputeScene(TestComputeScene&&) = default;
    TestComputeScene& operator=(TestComputeScene&&) = default;

    TestComputeScene(const TestComputeScene&) = delete;
    TestComputeScene& operator=(const TestComputeScene&) = delete;

public:
    virtual void setup() override;

    virtual vk::Semaphore::ptr render(uint32_t framebuffer_index,
                                      vk::Semaphore::ptr swap_chain_ready) override;

    virtual void teardown() override;

public:
    virtual void block_for_current_frame() override{};

private:
    void update_particles(vk::CommandBuffer::ptr cmd_buf);

private:
    vk::Fence::ptr frame_fence_;
    vk::Semaphore::ptr frame_finished_;

    vk::Semaphore::ptr compute_finished_;

    vk::CommandPool::ptr cmd_pool_;

    vk::CommandBuffer::ptr cmp_buffer_;

    vk::Buffer::ptr kernel_ubo_;
    std::unique_ptr<vk::PersistentMapping<KernelUBO>> kernel_ubo_mapping_;

    std::array<vk::Buffer::ptr, 2> particle_buffers_;

    vk::Kernel::ptr kernel_;

    vk::RenderPass::ptr render_pass_;
    vk::SwapChainFramebuffers::ptr framebuffers_;

    vk::GraphicsPipeline::ptr graphics_pipeline_;
};

}  // namespace spor