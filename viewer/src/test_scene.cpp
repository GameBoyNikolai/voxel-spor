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

void TestScene::setup() {
    mvp_ubo_ = vk::create_uniform_buffer(surface_device_, 1, sizeof(MVPUniformBuffer));
    mvp_mapping_ = std::make_unique<vk::PersistentMapping<MVPUniformBuffer>>(mvp_ubo_);

    cmd_pool_ = vk::CommandPool::create(surface_device_, surface_device_->queues.graphics);

    sampler_ = vk::Sampler::create(surface_device_);

    model_ = vk::Model::from_obj(surface_device_, cmd_pool_,
                                 "C:/Users/nicho/Downloads/viking_room.obj",
                                 "C:/Users/nicho/Downloads/viking_room.png");

    // descriptors_ = vk::PipelineDescriptors::create(
    //    surface_device_,
    //    {
    //        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT,
    //         vk::DescriptorInfo::DBuffer{mvp_ubo_, mvp_ubo_->size()}},
    //        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT,
    //         vk::DescriptorInfo::DSampler{model_->texture(), sampler_}},
    //    });

    render_pass_ = vk::RenderPass::create(surface_device_, swap_chain_,
                                          vk::DepthBuffer::default_format(surface_device_));

    global_desc_layout_ = vk::DescriptorLayout::create(
        surface_device_, {{0, vk::DescParameter::kUBO, VK_SHADER_STAGE_VERTEX_BIT}});

    graphics_pipeline_ = vk::GraphicsPipelineBuilder(surface_device_, swap_chain_, render_pass_)  //
                             .enable_depth_testing()                                              //
                             .add_vertex_shader(spor::shaders::test::vert)                        //
                             .add_fragment_shader(spor::shaders::test::frag)                      //
                             .set_vertex_descriptors(model_->vertex_binding_description(),        //
                                                     model_->vertex_attribute_descriptions())     //
                             .add_global_layout(global_desc_layout_)                              //
                             .add_local_layout({{0, vk::DescParameter::kSampledImage,             //
                                                 VK_SHADER_STAGE_FRAGMENT_BIT}})                  //
                             .build();

    desc_allocator_ = std::make_unique<vk::DescriptorAllocator>(
        surface_device_, 100,
        std::vector<vk::DescriptorAllocator::PoolSizeRatio>{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
        });

    global_desc_ = desc_allocator_->allocate(*global_desc_layout_);
    model_desc_ = desc_allocator_->allocate(*graphics_pipeline_->descriptor_layouts[1]);

    vk::DescriptorUpdater(surface_device_)  //
        .with_ubo(0, mvp_ubo_)              //
        .update(global_desc_);

    vk::DescriptorUpdater(surface_device_)                   //
        .with_sampled_image(0, model_->texture(), sampler_)  //
        .update(model_desc_);

    framebuffers_ = vk::SwapChainFramebuffers::create(surface_device_, swap_chain_, render_pass_);
}

void TestScene::render(CallSubmitter& submitter, uint32_t framebuffer_index) {
    update_uniform_buffers();

    auto cmd_buffer = cmd_pool_->primary_buffer(true);
    vk::record_commands rc(cmd_buffer);

    vk::transition_image(cmd_buffer, swap_chain_->image_view(framebuffer_index),
                         VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkRect2D view_rect{{0, 0}, swap_chain_->extent};
    {
        vk::start_rendering sr(cmd_buffer, view_rect, swap_chain_->image_view(framebuffer_index),
                               framebuffers_->depth_buffer->image_view());

        vkCmdBindPipeline(*cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          graphics_pipeline_->graphics_pipeline);

        vkCmdBindDescriptorSets(*cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                graphics_pipeline_->pipeline_layout, 0, 1,
                                &global_desc_.descriptor_set, 0, nullptr);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(view_rect.extent.width);
        viewport.height = static_cast<float>(view_rect.extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(*cmd_buffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swap_chain_->extent;
        vkCmdSetScissor(*cmd_buffer, 0, 1, &scissor);

        model_->draw(cmd_buffer, model_desc_, graphics_pipeline_);
    }

    vk::transition_image(cmd_buffer, swap_chain_->image_view(framebuffer_index),
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    submitter.submit_draw(surface_device_->queues.graphics, cmd_buffer);
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
    auto& ubo = (*mvp_mapping_)[0];
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
}

}  // namespace spor