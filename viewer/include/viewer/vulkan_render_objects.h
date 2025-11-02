#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "viewer/vulkan_helpers.h"
#include "vulkan/vulkan.h"

#include "viewer/vulkan_base_objects.h"

namespace spor::vk {

class GraphicsPipeline : public helpers::VulkanObject<GraphicsPipeline> {
public:
    ~GraphicsPipeline();

public:
    friend class GraphicsPipelineBuilder;

    operator VkPipeline() const { return graphics_pipeline; };

public:
    VkRenderPass render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;

private:
    SurfaceDevice::ptr surface_device_;
    SwapChain::ptr swap_chain_;

public:
    GraphicsPipeline(PrivateToken, SurfaceDevice::ptr surface_device, SwapChain::ptr swap_chain,
                     VkRenderPass render_pass, VkPipelineLayout pipeline_layout,
                     VkPipeline graphics_pipeline)
        : surface_device_(surface_device),
          swap_chain_(swap_chain),
          render_pass(render_pass),
          pipeline_layout(pipeline_layout),
          graphics_pipeline(graphics_pipeline) {}
};

class GraphicsPipelineBuilder {
public:
    GraphicsPipelineBuilder(SurfaceDevice::ptr surface_device, SwapChain::ptr swap_chain)
        : surface_device_(surface_device), swap_chain_(swap_chain) {}

    template <size_t N>
    GraphicsPipelineBuilder& add_vertex_shader(const std::array<uint32_t, N>& shader) {
        add_shader(VK_SHADER_STAGE_VERTEX_BIT, shader.data(), shader.size());

        return *this;
    }

    template <size_t N>
    GraphicsPipelineBuilder& add_fragment_shader(const std::array<uint32_t, N>& shader) {
        add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, shader.data(), shader.size());

        return *this;
    }

    GraphicsPipelineBuilder& set_vertex_descriptors(
        VkVertexInputBindingDescription binding_desc,
        std::vector<VkVertexInputAttributeDescription> attrib_descs);

    GraphicsPipelineBuilder& add_descriptor_set(VkDescriptorSetLayout descriptor_set);

    GraphicsPipeline::ptr build();

private:
    void add_shader(VkShaderStageFlagBits stage, const uint32_t* shader_data, size_t len);

private:
    SurfaceDevice::ptr surface_device_;
    SwapChain::ptr swap_chain_;

    std::vector<VkPipelineShaderStageCreateInfo> shader_stages_;
    std::vector<VkShaderModule> shaders_;

    // TODO: accept and store multiple descriptors
    struct VertexDescriptors {
        VkVertexInputBindingDescription binding_desc;
        std::vector<VkVertexInputAttributeDescription> attrib_descs;
    };
    std::optional<VertexDescriptors> vertex_descriptors_;

    std::vector<VkDescriptorSetLayout> descriptor_sets_;
};

class CommandPool : public helpers::VulkanObject<CommandPool> {
public:
    ~CommandPool();

public:
    operator VkCommandPool() const { return command_pool; }

    static ptr create(SurfaceDevice::ptr surface_device);

public:
    VkCommandPool command_pool;

private:
    SurfaceDevice::ptr surface_device_;

public:
    CommandPool(PrivateToken, SurfaceDevice::ptr surface_device, VkCommandPool pool)
        : surface_device_(surface_device), command_pool(pool) {}
};

class CommandBuffer : public helpers::VulkanObject<CommandBuffer> {
public:
    operator VkCommandBuffer() const { return command_buffer; }

    static ptr create(SurfaceDevice::ptr surface_device, CommandPool::ptr command_pool);

public:
    VkCommandBuffer command_buffer;

private:
    SurfaceDevice::ptr surface_device_;

public:
    CommandBuffer(PrivateToken, SurfaceDevice::ptr surface_device, VkCommandBuffer buffer)
        : surface_device_(surface_device), command_buffer(buffer) {}
};

class SwapChainFramebuffers : public helpers::VulkanObject<SwapChainFramebuffers> {
public:
    ~SwapChainFramebuffers();

public:
    static ptr create(SurfaceDevice::ptr surface_device, SwapChain::ptr swap_chain,
                      GraphicsPipeline::ptr pipeline);

public:
    std::vector<VkFramebuffer> framebuffers;

private:
    SurfaceDevice::ptr surface_device_;
    SwapChain::ptr swap_chain_;
    GraphicsPipeline::ptr pipeline_;

public:
    SwapChainFramebuffers(PrivateToken, SurfaceDevice::ptr surface_device,
                          SwapChain::ptr swap_chain, GraphicsPipeline::ptr pipeline,
                          std::vector<VkFramebuffer> framebuffers)
        : surface_device_(surface_device),
          swap_chain_(swap_chain),
          pipeline_(pipeline),
          framebuffers(std::move(framebuffers)) {}
};

class record_commands : helpers::NonCopyable {
public:
    explicit record_commands(CommandBuffer::ptr command_buffer);

    ~record_commands();

public:
    record_commands(record_commands&&) noexcept;
    record_commands& operator=(record_commands&&) noexcept;

private:
    CommandBuffer::ptr command_buffer_;
};

class render_pass : helpers::NonCopyable {
public:
    explicit render_pass(CommandBuffer::ptr command_buffer, GraphicsPipeline::ptr pipeline,
                         VkFramebuffer framebuffer, VkRect2D area);

    ~render_pass();

public:
    render_pass(render_pass&&) noexcept;
    render_pass& operator=(render_pass&&) noexcept;

private:
    CommandBuffer::ptr command_buffer_;
};

struct DefaultRenderSyncObjects : public helpers::VulkanObject<DefaultRenderSyncObjects> {
public:
    ~DefaultRenderSyncObjects();

public:
    static ptr create(vk::SurfaceDevice::ptr device);

public:
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;

private:
    vk::SurfaceDevice::ptr surface_device_;

public:
    DefaultRenderSyncObjects(PrivateToken, vk::SurfaceDevice::ptr surface_device,
                             VkSemaphore image_available, VkSemaphore render_finished,
                             VkFence in_flight)
        : surface_device_(surface_device),
          image_available(image_available),
          render_finished(render_finished),
          in_flight(in_flight) {}
};

}  // namespace spor::vk