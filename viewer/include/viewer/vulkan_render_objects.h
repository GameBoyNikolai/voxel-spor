#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "viewer/vulkan_base_objects.h"
#include "viewer/vulkan_helpers.h"
#include "vulkan/vulkan.h"

namespace spor::vk {

class RenderPass : public helpers::VulkanObject<RenderPass> {
public:
    ~RenderPass();

public:
    operator VkRenderPass() const { return render_pass; };

    static ptr create(SurfaceDevice::ptr device, VkFormat color_format,
                      std::optional<VkFormat> depth_stencil_format);

    static ptr create(SurfaceDevice::ptr device, SwapChain::ptr swap_chain,
                      std::optional<VkFormat> depth_stencil_format) {
        return create(device, swap_chain->format, depth_stencil_format);
    }

public:
    VkRenderPass render_pass;

    VkFormat color_format;
    std::optional<VkFormat> depth_stencil_format;

private:
    SurfaceDevice::ptr device_;

public:
    RenderPass(PrivateToken, SurfaceDevice::ptr device, VkRenderPass render_pass,
               VkFormat color_format, std::optional<VkFormat> depth_stencil_format)
        : device_(device),
          render_pass(render_pass),
          color_format(color_format),
          depth_stencil_format(depth_stencil_format) {}
};

// TODO: Separate the descriptor nonsense into parameters and arguments, similar to the kernel
class GraphicsPipeline : public helpers::VulkanObject<GraphicsPipeline> {
public:
    ~GraphicsPipeline();

public:
    friend class GraphicsPipelineBuilder;

    operator VkPipeline() const { return graphics_pipeline; };

public:
    VkPipelineLayout pipeline_layout;
    VkPipeline graphics_pipeline;

private:
    SurfaceDevice::ptr surface_device_;
    SwapChain::ptr swap_chain_;
    RenderPass::ptr render_pass_;

public:
    GraphicsPipeline(PrivateToken, SurfaceDevice::ptr surface_device, SwapChain::ptr swap_chain,
                     RenderPass::ptr render_pass, VkPipelineLayout pipeline_layout,
                     VkPipeline graphics_pipeline)
        : surface_device_(surface_device),
          swap_chain_(swap_chain),
          render_pass_(render_pass),
          pipeline_layout(pipeline_layout),
          graphics_pipeline(graphics_pipeline) {}
};

class GraphicsPipelineBuilder {
public:
    GraphicsPipelineBuilder(SurfaceDevice::ptr surface_device, SwapChain::ptr swap_chain,
                            RenderPass::ptr render_pass)
        : surface_device_(surface_device), swap_chain_(swap_chain), render_pass_(render_pass) {}

    GraphicsPipelineBuilder& add_vertex_shader(const std::vector<uint32_t>& shader);

    GraphicsPipelineBuilder& add_fragment_shader(const std::vector<uint32_t>& shader);

    GraphicsPipelineBuilder& set_vertex_descriptors(
        VkVertexInputBindingDescription binding_desc,
        std::vector<VkVertexInputAttributeDescription> attrib_descs);

    GraphicsPipelineBuilder& set_primitive_type(VkPrimitiveTopology primitive_type);

    GraphicsPipelineBuilder& add_descriptor_set(VkDescriptorSetLayout descriptor_set);

    GraphicsPipelineBuilder& enable_depth_testing();

    GraphicsPipeline::ptr build();

private:
    void add_shader(VkShaderStageFlagBits stage, const uint32_t* shader_data, size_t len);

private:
    SurfaceDevice::ptr surface_device_;
    SwapChain::ptr swap_chain_;
    RenderPass::ptr render_pass_;

    std::vector<VkPipelineShaderStageCreateInfo> shader_stages_;
    std::vector<VkShaderModule> shaders_;

    // TODO: accept and store multiple descriptors
    struct VertexDescriptors {
        VkVertexInputBindingDescription binding_desc;
        std::vector<VkVertexInputAttributeDescription> attrib_descs;
    };
    std::optional<VertexDescriptors> vertex_descriptors_;

    VkPrimitiveTopology primitive_type_ = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    std::vector<VkDescriptorSetLayout> descriptor_sets_;

    bool depth_testing_ = false;
};

class DepthBuffer : public helpers::VulkanObject<DepthBuffer> {
public:
    ~DepthBuffer();

public:
    static VkFormat default_format(SurfaceDevice::ptr surface_device);

    static ptr create(SurfaceDevice::ptr surface_device, SwapChain::ptr swap_chain,
                      VkFormat format);

    static ptr create(SurfaceDevice::ptr surface_device, SwapChain::ptr swap_chain);

public:
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;

private:
    SurfaceDevice::ptr surface_device_;
    SwapChain::ptr swap_chain_;

public:
    DepthBuffer(PrivateToken, SurfaceDevice::ptr surface_device, SwapChain::ptr swap_chain,
                VkImage image, VkImageView view, VkDeviceMemory memory)
        : surface_device_(surface_device),
          swap_chain_(swap_chain),
          image(image),
          view(view),
          memory(memory) {}
};

class SwapChainFramebuffers : public helpers::VulkanObject<SwapChainFramebuffers> {
public:
    ~SwapChainFramebuffers();

public:
    static ptr create(SurfaceDevice::ptr surface_device, SwapChain::ptr swap_chain,
                      RenderPass::ptr render_pass);

public:
    std::vector<VkFramebuffer> framebuffers;
    DepthBuffer::ptr depth_buffer;

private:
    SurfaceDevice::ptr surface_device_;
    SwapChain::ptr swap_chain_;
    RenderPass::ptr render_pass_;

public:
    SwapChainFramebuffers(PrivateToken, SurfaceDevice::ptr surface_device,
                          SwapChain::ptr swap_chain, RenderPass::ptr render_pass,
                          std::vector<VkFramebuffer> framebuffers, DepthBuffer::ptr depth_buffer)
        : surface_device_(surface_device),
          swap_chain_(swap_chain),
          render_pass_(render_pass),
          framebuffers(std::move(framebuffers)),
          depth_buffer(depth_buffer) {}
};

class begin_render_pass : helpers::NonCopyable {
public:
    explicit begin_render_pass(CommandBuffer::ptr command_buffer, RenderPass::ptr render_pass,
                               VkFramebuffer framebuffer, VkRect2D area);

    ~begin_render_pass();

public:
    begin_render_pass(begin_render_pass&&) noexcept;
    begin_render_pass& operator=(begin_render_pass&&) noexcept;

private:
    CommandBuffer::ptr command_buffer_;
};

class Fence : public helpers::VulkanObject<Fence> {
public:
    ~Fence();

public:
    static ptr create(vk::SurfaceDevice::ptr device);

    operator VkFence() { return fence; }

public:
    VkFence fence;

private:
    vk::SurfaceDevice::ptr surface_device_;

public:
    Fence(PrivateToken, vk::SurfaceDevice::ptr device, VkFence fence)
        : surface_device_(device), fence(fence) {}
};

class Semaphore : public helpers::VulkanObject<Semaphore> {
public:
    ~Semaphore();

public:
    static ptr create(vk::SurfaceDevice::ptr device);

    operator VkSemaphore() { return semaphore; }

public:
    VkSemaphore semaphore;

private:
    vk::SurfaceDevice::ptr surface_device_;

public:
    Semaphore(PrivateToken, vk::SurfaceDevice::ptr device, VkSemaphore semaphore)
        : surface_device_(device), semaphore(semaphore) {}
};

class DefaultRenderSyncObjects : public helpers::VulkanObject<DefaultRenderSyncObjects> {
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