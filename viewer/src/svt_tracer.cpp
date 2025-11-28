#include "viewer\svt_tracer.h"

#include <array>
#include <cstddef>
#include <iostream>

#include "shaders/sv_trace.comp.inl"
#include "tiny_obj_loader.h"
#include "viewer/image_loader.h"
#include "vkh/glm_decl.h"

namespace spor {

void SvtTracerScene::setup() {
    frame_fence_ = vk::Fence::create(surface_device_);
    frame_finished_ = vk::Semaphore::create(surface_device_);
    compute_finished_ = vk::Semaphore::create(surface_device_);

    depth_buffer_ = vk::DepthBuffer::create(surface_device_, swap_chain_->extent.width,
                                            swap_chain_->extent.height);
    draw_image_ = vk::DrawImage::create(surface_device_, swap_chain_->extent.width,
                                        swap_chain_->extent.height);

    tracer_ubo_ = vk::create_uniform_buffer(surface_device_, 1, sizeof(TracerUBO));
    tracer_ubo_mapping_ = std::make_unique<vk::PersistentMapping<TracerUBO>>(tracer_ubo_);

    cmd_pool_ = vk::CommandPool::create(surface_device_, surface_device_->queues.graphics);

    cmp_buffer_ = vk::CommandBuffer::create(surface_device_, cmd_pool_);

    sampler_ = vk::Sampler::create(surface_device_);

    vdb_ = std::make_unique<vox::VDB>(surface_device_);
    {
        constexpr size_t kRadius = 105.f;
        constexpr size_t kSize = 256;

        const vox::VDB::coord_t center(kSize / 2);

        auto sampler = [center, kRadius](vox::VDB::coord_t pos) -> uint8_t {
            int dist = static_cast<int>(glm::distance(glm::vec3(center), glm::vec3(pos)));
            if (dist > kRadius) {
                return 0;
            } else {
                return dist + 1;
            }
        };

        vdb_->build_from(vox::VDB::coord_t(kSize), sampler);
        vdb_->move_to_device(cmd_pool_);
    }

    trace_func_ = vk::Kernel::create(
        surface_device_, shaders::sv_trace::comp,
        {vk::Kernel::ParamType::kUBO, vk::Kernel::ParamType::kSSBO, vk::Kernel::ParamType::kSSBO,
         vk::Kernel::ParamType::kSSBO, vk::Kernel::ParamType::kStorageImage});

    full_desc_layout_ = vk::DescriptorLayout::create(
        surface_device_,
        {
            {0, vk::DescParameter::kUBO, VK_SHADER_STAGE_COMPUTE_BIT},  // TracerUBO

            {1, vk::DescParameter::kSSBO, VK_SHADER_STAGE_COMPUTE_BIT},  // VDB Info
            {2, vk::DescParameter::kSSBO, VK_SHADER_STAGE_COMPUTE_BIT},  // VDB Nodes
            {3, vk::DescParameter::kSSBO, VK_SHADER_STAGE_COMPUTE_BIT},  // VDB Voxels

            {4, vk::DescParameter::kStorageImage, VK_SHADER_STAGE_COMPUTE_BIT},  // out image
        });

    desc_allocator_ = std::make_unique<vk::DescriptorAllocator>(
        surface_device_, 100,
        std::vector<vk::DescriptorAllocator::PoolSizeRatio>{
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
        });

    full_desc_ = desc_allocator_->allocate(*full_desc_layout_)
                     .with_ubo(0, tracer_ubo_)                          //
                     .with_ssbo(1, vdb_->info_buffer())                 //
                     .with_ssbo(2, vdb_->node_buffer())                 //
                     .with_ssbo(3, vdb_->voxel_buffer())                //
                     .with_storage_image(4, draw_image_->image_view())  //
                     .update();                                         //
}

vk::Semaphore::ptr SvtTracerScene::render(uint32_t framebuffer_index,
                                          vk::Semaphore::ptr swap_chain_ready) {
    update_uniform_buffers();

    auto cmd_buffer = cmd_pool_->primary_buffer(true);
    {
        vk::record_commands rc(cmd_buffer);

        // vk::transition_image(cmd_buffer, draw_image_->image_view(), VK_IMAGE_LAYOUT_UNDEFINED,
        //                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        // vk::transition_image(cmd_buffer, depth_buffer_->image_view(), VK_IMAGE_LAYOUT_UNDEFINED,
        //                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        vk::transition_image(cmd_buffer, draw_image_->image_view(), VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_GENERAL);
        vk::transition_image(cmd_buffer, depth_buffer_->image_view(), VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        size_t x_ts = swap_chain_->extent.width / 16 + (swap_chain_->extent.width % 16 > 0 ? 1 : 0);
        size_t y_ts
            = swap_chain_->extent.height / 16 + (swap_chain_->extent.height % 16 > 0 ? 1 : 0);

        trace_func_->invoke(cmd_buffer, full_desc_, glm::u64vec3(x_ts, y_ts, 1));

        vk::transition_image(cmd_buffer, draw_image_->image_view(), VK_IMAGE_LAYOUT_GENERAL,
                             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        vk::transition_image(cmd_buffer, swap_chain_->image_view(framebuffer_index),
                             VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vk::blit_image(cmd_buffer, draw_image_->image_view(),
                       swap_chain_->image_view(framebuffer_index));

        vk::transition_image(cmd_buffer, swap_chain_->image_view(framebuffer_index),
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }

    {
        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        std::vector<VkSemaphore> wait_semaphores = {*swap_chain_ready};
        std::vector<VkPipelineStageFlags> wait_stages
            = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

        submit_info.waitSemaphoreCount = static_cast<uint32_t>(wait_semaphores.size());
        submit_info.pWaitSemaphores = wait_semaphores.data();
        submit_info.pWaitDstStageMask = wait_stages.data();

        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buffer->command_buffer;

        VkSemaphore signal_semaphores[] = {*frame_finished_};
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = signal_semaphores;

        vk::helpers::check_vulkan(vkQueueSubmit(surface_device_->queues.graphics.queue, 1,
                                                &submit_info, frame_fence_->fence));
    }

    return frame_finished_;
}

void SvtTracerScene::teardown() {}

void SvtTracerScene::block_for_current_frame() {
    vkWaitForFences(*surface_device_, 1, &frame_fence_->fence, VK_TRUE,
                    std::numeric_limits<uint64_t>::max());
    vkResetFences(*surface_device_, 1, &frame_fence_->fence);
}

void SvtTracerScene::on_mouse_drag(MouseButton button, glm::vec2 offset) {
    constexpr float kSpeed = glm::radians(0.1f);
    if (button == MouseButton::kLeft) {
        orbit_rot_ += glm::vec2(-1, 1) * offset * kSpeed;
        orbit_rot_.y = glm::clamp(orbit_rot_.y, -glm::pi<float>() / 2.f + glm::epsilon<float>(),
                                  glm::pi<float>() / 2.f - glm::epsilon<float>());
    }
}

void SvtTracerScene::on_mouse_scroll(float offset) {
    constexpr float kSpeed = 0.5f;
    orbit_radius_ += offset * kSpeed;
}

void SvtTracerScene::update_uniform_buffers() {
    auto& ubo = (*tracer_ubo_mapping_)[0];
    ubo.model = glm::scale(glm::mat4(1.0), glm::vec3(0.2));
    ubo.model = glm::translate(ubo.model, -glm::vec3(vdb_->size()) / 2.f);

    ubo.camera_pos
        = orbit_radius_
          * glm::vec3(glm::cos(orbit_rot_.x) * glm::cos(orbit_rot_.y),
                      glm::sin(orbit_rot_.x) * glm::cos(orbit_rot_.y), glm::sin(orbit_rot_.y));

    ubo.view = glm::lookAt(ubo.camera_pos,               //
                           glm::vec3(0.0f, 0.0f, 0.0f),  //
                           glm::vec3(0.0f, 0.0f, 1.0f));

    ubo.projection = glm::perspective(
        glm::radians(45.0f),
        swap_chain_->extent.width / static_cast<float>(swap_chain_->extent.height), 0.1f, 1000.0f);

    // Vulkan uses a flipped Projection space Y compared to OGL
    ubo.projection[1][1] *= -1.f;

    auto view_no_trans
        = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), -ubo.camera_pos, glm::vec3(0.0f, 0.0f, 1.0f));

    ubo.inv_vp = glm::inverse(ubo.projection * view_no_trans);
    ubo.inv_m = glm::inverse(ubo.model);
}

}  // namespace spor