#pragma once

#include "viewer/vulkan_application.h"
#include "viewer/vulkan_objects.h"

namespace spor {

class TestScene : public Scene {
public:
    virtual void setup();

    virtual vk::CommandBuffer::ptr render(uint32_t framebuffer_index);

    virtual void teardown();

private:
    vk::GraphicsPipeline::ptr graphics_pipeline_;
    vk::SwapChainFramebuffers::ptr framebuffers_;

    vk::CommandPool::ptr pool_;
    vk::CommandBuffer::ptr cmd_buffer_;
};

}