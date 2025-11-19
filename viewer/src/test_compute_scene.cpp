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

namespace {
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
}  // namespace

void TestComputeScene::setup() {
    // mvp_ubo_ = vk::create_uniform_buffer(surface_device_, 1, sizeof(UniformBufferObject));
    // mvp_mapping_ = std::make_unique<vk::PersistentMapping>(mvp_ubo_);

    gfx_cmd_pool_ = vk::CommandPool::create(surface_device_, surface_device_->queues.graphics);
    cmp_cmd_pool_ = vk::CommandPool::create(surface_device_, surface_device_->queues.graphics);
    gfx_cmd_buffer_ = vk::CommandBuffer::create(surface_device_, gfx_cmd_pool_);
    cmp_cmd_buffer_ = vk::CommandBuffer::create(surface_device_, cmp_cmd_pool_);

    kernel_ubo_ = vk::create_uniform_buffer(surface_device_, 1, sizeof(KernelUBO));
    kernel_ubo_mapping_ = std::make_unique<vk::PersistentMapping>(kernel_ubo_);

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

            float r = 0.25f * std::sqrt(dist(eng));
            float theta = dist(eng) * 2.0f * glm::pi<float>();
            float x = r * std::cos(theta) * static_cast<float>(swap_chain_->extent.height) / swap_chain_->extent.width;
            float y = r * std::sin(theta);
            particle.position = glm::vec2(x, y);
            particle.velocity = glm::normalize(glm::vec2(x, y)) * 0.055f;
            particle.color = glm::vec4(dist(eng), dist(eng), dist(eng), 1.0f);
        }

        for (const auto& p_buf : particle_buffers_) {
            auto transfer_buf
                = vk::create_and_fill_transfer_buffer(surface_device_, particle_init_data);
            vk::submit_commands(vk::buffer_memcpy(surface_device_, gfx_cmd_pool_, transfer_buf,
                                                  p_buf, p_buf->size()), surface_device_->queues.graphics.queue);
        }
    }

    kernel_ = vk::Kernel::create(
        surface_device_, spor::shaders::particles::comp,
        {vk::Kernel::ParamType::kUBO, vk::Kernel::ParamType::kSSBO, vk::Kernel::ParamType::kSSBO});

    render_pass_ = vk::RenderPass::create(surface_device_, swap_chain_,
                                          vk::DepthBuffer::default_format(surface_device_));

    graphics_pipeline_ = vk::GraphicsPipelineBuilder(surface_device_, swap_chain_, render_pass_)  //
                             //.enable_depth_testing()                                              //
                             .add_vertex_shader(spor::shaders::particles::vert)                   //
                             .add_fragment_shader(spor::shaders::particles::frag)                 //
                             .set_vertex_descriptors(Particle::binding_description(),             //
                                                     Particle::attribute_descriptions())          //
                             .set_primitive_type(VK_PRIMITIVE_TOPOLOGY_POINT_LIST)                //
                             .build();

    framebuffers_ = vk::SwapChainFramebuffers::create(surface_device_, swap_chain_, render_pass_);
}

void TestComputeScene::render(CallSubmitter& submitter, uint32_t framebuffer_index) {
    submitter.submit_compute(surface_device_->queues.graphics, update_particles());

    vk::record_commands rc(gfx_cmd_buffer_);

    VkRect2D view_rect{{0, 0}, swap_chain_->extent};
    vk::begin_render_pass rp(gfx_cmd_buffer_, render_pass_,
                             framebuffers_->framebuffers[framebuffer_index], view_rect);

    vkCmdBindPipeline(*gfx_cmd_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      graphics_pipeline_->graphics_pipeline);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(view_rect.extent.width);
    viewport.height = static_cast<float>(view_rect.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(*gfx_cmd_buffer_, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swap_chain_->extent;
    vkCmdSetScissor(*gfx_cmd_buffer_, 0, 1, &scissor);

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(*gfx_cmd_buffer_, 0, 1, &particle_buffers_[0]->buffer, offsets);

    vkCmdDraw(*gfx_cmd_buffer_, kNumParticles, 1, 0, 0);

    submitter.submit_draw(surface_device_->queues.graphics, gfx_cmd_buffer_);
}

void TestComputeScene::teardown() {}

vk::CommandBuffer::ptr TestComputeScene::update_particles() {
    // slightly prettier to me than a memcpy?
    // TODO: perhaps make PersistentMapping a template type
    *reinterpret_cast<KernelUBO*>(kernel_ubo_mapping_->mapped_mem)
        = {0.01666f, static_cast<uint32_t>(kNumParticles)};

    // swap in and out buffers
    std::swap(particle_buffers_[0], particle_buffers_[1]);

    vk::helpers::check_vulkan(vkResetCommandBuffer(*cmp_cmd_buffer_, 0));
    vk::Invoker(kernel_)                  //
        .with_ubo(kernel_ubo_)            //
        .with_ssbo(particle_buffers_[0])  //
        .with_ssbo(particle_buffers_[1])  //
        .invoke(cmp_cmd_buffer_, kNumParticles / 1024 + (kNumParticles % 1024 > 0 ? 1 : 0));

    return cmp_cmd_buffer_;
}

}  // namespace spor