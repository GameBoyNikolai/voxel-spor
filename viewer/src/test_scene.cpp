#include "viewer\test_scene.h"

#include <array>
#include <cstddef>
#include <iostream>

#include "shaders/test.frag.inl"
#include "shaders/test.vert.inl"
#include "tiny_obj_loader.h"
#include "viewer/glm_decl.h"
#include "viewer/image_loader.h"

namespace spor {

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

vk::Texture::ptr load_texture(vk::SurfaceDevice::ptr device, vk::CommandPool::ptr pool,
                              VkQueue queue, const std::string& path) {
    int width, height, channels;
    stbi_uc* image_data = stbi_load(path.data(), &width, &height, &channels, STBI_rgb_alpha);

    auto transfer_buf = vk::create_and_fill_transfer_buffer(
        device, image_data, static_cast<size_t>(width * height * STBI_rgb_alpha));

    stbi_image_free(image_data);

    auto texture = vk::Texture::create(device, width, height);
    vk::submit_commands(vk::transition_texture(device, pool, texture, VK_IMAGE_LAYOUT_UNDEFINED,
                                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
                        queue);

    vk::submit_commands(vk::texture_memcpy(device, pool, transfer_buf, texture), queue);

    vk::submit_commands(
        vk::transition_texture(device, pool, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
        queue);

    return texture;
}

void TestScene::setup() {
    mvp_ubo_ = vk::create_uniform_buffer(surface_device_, 1, sizeof(UniformBufferObject));
    mvp_mapping_ = std::make_unique<vk::PersistentMapping>(mvp_ubo_);

    cmd_pool_ = vk::CommandPool::create(surface_device_);
    cmd_buffer_ = vk::CommandBuffer::create(surface_device_, cmd_pool_);

    sampler_ = vk::Sampler::create(surface_device_);

    model_ = vk::Model::from_obj(surface_device_, cmd_pool_,
                                 "C:/Users/nicho/Downloads/viking_room.obj",
                                 "C:/Users/nicho/Downloads/viking_room.png");

    descriptors_ = vk::PipelineDescriptors::create(
        surface_device_,
        {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT,
             vk::DescriptorInfo::DBuffer{mvp_ubo_, mvp_ubo_->size()}},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
             vk::DescriptorInfo::DSampler{model_->texture(), sampler_}},
        });

    render_pass_ = vk::RenderPass::create(surface_device_, swap_chain_,
                                          vk::DepthBuffer::default_format(surface_device_));

    graphics_pipeline_ = vk::GraphicsPipelineBuilder(surface_device_, swap_chain_, render_pass_)  //
                             .enable_depth_testing()                                              //
                             .add_vertex_shader(spor::shaders::test::vert)                        //
                             .add_fragment_shader(spor::shaders::test::frag)                      //
                             .set_vertex_descriptors(model_->vertex_binding_description(),        //
                                                     model_->vertex_attribute_descriptions())     //
                             .add_descriptor_set(descriptors_->layout)                            //
                             .build();

    framebuffers_ = vk::SwapChainFramebuffers::create(surface_device_, swap_chain_, render_pass_);
}

vk::CommandBuffer::ptr TestScene::render(uint32_t framebuffer_index) {
    update_uniform_buffers();

    vk::record_commands rc(cmd_buffer_);

    VkRect2D view_rect{{0, 0}, swap_chain_->extent};
    vk::begin_render_pass rp(cmd_buffer_, render_pass_,
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

    model_->draw(cmd_buffer_, descriptors_, graphics_pipeline_);

    return cmd_buffer_;
}

void TestScene::teardown() {}

void TestScene::on_mouse_drag(MouseButton button, glm::vec2 offset) {
    constexpr float kSpeed = glm::radians(0.1f);
    if (button == MouseButton::kLeft) {
        orbit_rot_ += glm::vec2(-1, 1) * offset * kSpeed;
        orbit_rot_.y = glm::clamp(orbit_rot_.y, -glm::pi<float>() / 2.f + glm::epsilon<float>(),
                                  glm::pi<float>() / 2.f - glm::epsilon<float>());
    }
}

void TestScene::on_mouse_scroll(float offset) {
    constexpr float kSpeed = 0.1f;
    orbit_radius_ += offset * kSpeed;
}

void TestScene::update_uniform_buffers() {
    UniformBufferObject ubo{};
    ubo.model = model_->xfm;
    ubo.view = glm::lookAt(orbit_radius_
                               * glm::vec3(glm::cos(orbit_rot_.x) * glm::cos(orbit_rot_.y),
                                           glm::sin(orbit_rot_.x) * glm::cos(orbit_rot_.y),
                                           glm::sin(orbit_rot_.y)),  //
                           glm::vec3(0.0f, 0.0f, 0.0f),              //
                           glm::vec3(0.0f, 0.0f, 1.0f));

    ubo.projection = glm::perspective(
        glm::radians(45.0f),
        swap_chain_->extent.width / static_cast<float>(swap_chain_->extent.height), 0.1f, 1000.0f);

    // Vulkan uses a flipped Projection space Y compared to OGL
    ubo.projection[1][1] *= -1.f;

    // slightly prettier to me than a memcpy?
    // TODO: perhaps make PersistentMapping a template type
    *reinterpret_cast<UniformBufferObject*>(mvp_mapping_->mapped_mem) = ubo;
}

}  // namespace spor