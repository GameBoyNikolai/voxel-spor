#pragma once

#include <array>

#include "viewer/vulkan_buffer_objects.h"
#include "viewer/vulkan_application.h"
#include "viewer/vulkan_base_objects.h"
#include "viewer/vulkan_render_objects.h"
#include "viewer/vulkan_compute.h"
#include "viewer/model.h"

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

    virtual void render(CallSubmitter& submitter, uint32_t framebuffer_index) override;

    virtual void teardown() override;

private:
    vk::CommandBuffer::ptr update_particles();

private:
    vk::CommandPool::ptr gfx_cmd_pool_, cmp_cmd_pool_;

    vk::Buffer::ptr kernel_ubo_;
    std::unique_ptr<vk::PersistentMapping<KernelUBO>> kernel_ubo_mapping_;

    std::array<vk::Buffer::ptr, 2> particle_buffers_;

    vk::Kernel::ptr kernel_;

    vk::RenderPass::ptr render_pass_;
    vk::SwapChainFramebuffers::ptr framebuffers_;

    vk::GraphicsPipeline::ptr graphics_pipeline_;
};

}  // namespace spor