#include "viewer\test_compute_scene.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <random>

#include "shaders/particles.comp.inl"
#include "shaders/particles.frag.inl"
#include "shaders/particles.vert.inl"
#include "tiny_obj_loader.h"
#include "viewer/glm_decl.h"
#include "viewer/image_loader.h"

namespace spor {

struct KernelUBO {
    float dt = 0.f;
    uint32_t num_particles = 0;
};

struct Particle {
    glm::vec2 position;
    glm::vec2 velocity;
    glm::vec4 color;

    static VkVertexInputBindingDescription binding_description() {
        VkVertexInputBindingDescription desc{};
        desc.binding = 0;
        desc.stride = sizeof(Particle);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return desc;
    }

    static std::vector<VkVertexInputAttributeDescription> attribute_descriptions() {
        std::vector<VkVertexInputAttributeDescription> descs(2);

        descs[0].binding = 0;
        descs[0].location = 0;
        descs[0].format = VK_FORMAT_R32G32_SFLOAT;
        descs[0].offset = offsetof(Particle, position);

        descs[1].binding = 0;
        descs[1].location = 1;
        descs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        descs[1].offset = offsetof(Particle, color);

        return descs;
    }
};

void TestComputeScene::setup() {
    frame_fence_ = vk::Fence::create(surface_device_);
    frame_finished_ = vk::Semaphore::create(surface_device_);
    compute_finished_ = vk::Semaphore::create(surface_device_);

    cmd_pool_ = vk::CommandPool::create(surface_device_, surface_device_->queues.graphics);

    cmp_buffer_ = vk::CommandBuffer::create(surface_device_, cmd_pool_);

    kernel_ubo_ = vk::create_uniform_buffer(surface_device_, 1, sizeof(KernelUBO));
    kernel_ubo_mapping_ = std::make_unique<vk::PersistentMapping<KernelUBO>>(kernel_ubo_);

    particle_buffers_ = {{
        vk::create_storage_buffer(surface_device_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kNumParticles,
                                  sizeof(Particle)),
        vk::create_storage_buffer(surface_device_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kNumParticles,
                                  sizeof(Particle)),
    }};

    {
        std::default_random_engine eng(0);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        std::vector<Particle> particle_init_data(kNumParticles);
        for (size_t i = 0; i < kNumParticles; ++i) {
            auto& particle = particle_init_data[i];

            float r = 0.1f * std::sqrt(dist(eng));
            float theta = dist(eng) * 2.0f * glm::pi<float>();
            float x = r * std::cos(theta) * static_cast<float>(swap_chain_->extent.height)
                      / swap_chain_->extent.width;
            float y = r * std::sin(theta);
            particle.position = glm::vec2(x, y);
            particle.velocity = glm::normalize(glm::vec2(x, y)) * 0.055f;
            particle.color = glm::vec4(dist(eng), dist(eng), dist(eng), 1.0f);
        }

        for (const auto& p_buf : particle_buffers_) {
            auto transfer_buf
                = vk::create_and_fill_transfer_buffer(surface_device_, particle_init_data);
            vk::submit_commands(
                vk::buffer_memcpy(surface_device_, cmd_pool_, transfer_buf, p_buf, p_buf->size()),
                surface_device_->queues.graphics.queue);
        }
    }

    kernel_ = vk::Kernel::create(
        surface_device_, spor::shaders::particles::comp,
        {vk::Kernel::ParamType::kUBO, vk::Kernel::ParamType::kSSBO, vk::Kernel::ParamType::kSSBO});

    desc_allocator_ = std::make_unique<vk::DescriptorAllocator>(
        surface_device_, 100,
        std::vector<vk::DescriptorAllocator::PoolSizeRatio>{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
        });

    particle_descs_ = {{
        desc_allocator_->allocate(kernel_->parameter_layout())
            .with_ubo(0, kernel_ubo_)            //
            .with_ssbo(1, particle_buffers_[0])  //
            .with_ssbo(2, particle_buffers_[1])  //
            .update(),                           //
        desc_allocator_->allocate(kernel_->parameter_layout())
            .with_ubo(0, kernel_ubo_)            //
            .with_ssbo(1, particle_buffers_[1])  //
            .with_ssbo(2, particle_buffers_[0])  //
            .update(),                           //
    }};

    render_pass_ = vk::RenderPass::create(surface_device_, swap_chain_,
                                          vk::DepthBuffer::default_format(surface_device_));

    graphics_pipeline_ = vk::GraphicsPipelineBuilder(surface_device_, swap_chain_,
                                                     render_pass_)  //
                                                                    //.enable_depth_testing() //
                             .add_vertex_shader(spor::shaders::particles::vert)           //
                             .add_fragment_shader(spor::shaders::particles::frag)         //
                             .set_vertex_descriptors(Particle::binding_description(),     //
                                                     Particle::attribute_descriptions())  //
                             .set_primitive_type(VK_PRIMITIVE_TOPOLOGY_POINT_LIST)        //
                             .build();

    framebuffers_ = vk::SwapChainFramebuffers::create(surface_device_, swap_chain_, render_pass_);
}

vk::Semaphore::ptr TestComputeScene::render(uint32_t framebuffer_index,
                                            vk::Semaphore::ptr swap_chain_ready) {
    {
        vk::helpers::check_vulkan(vkResetCommandBuffer(*cmp_buffer_, 0));

        {
            vk::record_commands rc(cmp_buffer_);
            update_particles(cmp_buffer_);
        }

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmp_buffer_->command_buffer;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &compute_finished_->semaphore;

        vk::helpers::check_vulkan(
            vkQueueSubmit(surface_device_->queues.graphics.queue, 1, &submit_info, nullptr));
    }

    {
        auto cmds = cmd_pool_->primary_buffer(true);
        {
            vk::record_commands rc(cmds);

            VkRect2D view_rect{{0, 0}, swap_chain_->extent};
            vk::begin_render_pass rp(cmds, render_pass_,
                                     framebuffers_->framebuffers[framebuffer_index], view_rect);

            vkCmdBindPipeline(*cmds, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              graphics_pipeline_->graphics_pipeline);

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(view_rect.extent.width);
            viewport.height = static_cast<float>(view_rect.extent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(*cmds, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = swap_chain_->extent;
            vkCmdSetScissor(*cmds, 0, 1, &scissor);

            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(*cmds, 0, 1, &particle_buffers_[0]->buffer, offsets);

            vkCmdDraw(*cmds, kNumParticles, 1, 0, 0);
        }

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        std::vector<VkSemaphore> wait_semaphores = {*swap_chain_ready, *compute_finished_};
        std::vector<VkPipelineStageFlags> wait_stages
            = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT};

        submit_info.waitSemaphoreCount = static_cast<uint32_t>(wait_semaphores.size());
        submit_info.pWaitSemaphores = wait_semaphores.data();
        submit_info.pWaitDstStageMask = wait_stages.data();

        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmds->command_buffer;

        VkSemaphore signal_semaphores[] = {*frame_finished_};
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = signal_semaphores;

        vk::helpers::check_vulkan(vkQueueSubmit(surface_device_->queues.graphics.queue, 1,
                                                &submit_info, frame_fence_->fence));
    }

    return frame_finished_;
}

void TestComputeScene::teardown() {}

void TestComputeScene::block_for_current_frame() {
    vkWaitForFences(*surface_device_, 1, &frame_fence_->fence, VK_TRUE,
                    std::numeric_limits<uint64_t>::max());
    vkResetFences(*surface_device_, 1, &frame_fence_->fence);
}

void TestComputeScene::update_particles(vk::CommandBuffer::ptr cmd_buf) {
    (*kernel_ubo_mapping_)[0] = {0.01666f, static_cast<uint32_t>(kNumParticles)};

    // swap in and out buffers
    std::swap(particle_descs_[0], particle_descs_[1]);

    kernel_->invoke(cmd_buf, particle_descs_[0],
                    kNumParticles / 1024 + (kNumParticles % 1024 > 0 ? 1 : 0));
}

}  // namespace spor