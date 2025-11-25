#pragma once

#include <list>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "vkh/glm_decl.h"
#include "vkh/base_objects.h"
#include "vkh/buffer_objects.h"
#include "vkh/helpers.h"
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

struct DescriptorSet {
public:
    VkDescriptorSet descriptor_set;

public:
    operator VkDescriptorSet() const { return descriptor_set; }
};

class DescriptorUpdater : public helpers::NonCopyable {
public:
    DescriptorUpdater(SurfaceDevice::ptr device, VkDescriptorSet desc);
    ~DescriptorUpdater() = default;

    DescriptorUpdater(DescriptorUpdater&&) noexcept = default;
    DescriptorUpdater& operator=(DescriptorUpdater&&) noexcept = default;

public:
    DescriptorUpdater& with_ubo(uint32_t binding, Buffer::ptr buffer,
                                std::optional<size_t> offset = std::nullopt,
                                std::optional<size_t> size = std::nullopt);
    DescriptorUpdater& with_ssbo(uint32_t binding, Buffer::ptr buffer,
                                 std::optional<size_t> offset = std::nullopt,
                                 std::optional<size_t> size = std::nullopt);
    DescriptorUpdater& with_sampled_image(uint32_t binding, Texture::ptr texture,
                                          vk::Sampler::ptr sampler,
                                          std::optional<VkImageLayout> layout = std::nullopt);
    DescriptorUpdater& with_storage_image(uint32_t binding, Texture::ptr texture,
                                          std::optional<VkImageLayout> layout = std::nullopt);

    DescriptorSet update();

    DescriptorSet get();

private:
    VkWriteDescriptorSet& add_write(VkDescriptorType type);

    SurfaceDevice::ptr device_;

    VkDescriptorSet desc_to_update_;

    // lists allow us to safely keep pointers to the elements
    std::list<VkDescriptorImageInfo> image_infos_;
    std::list<VkDescriptorBufferInfo> buffer_infos_;
    std::vector<VkWriteDescriptorSet> writes_;
};

struct DescParameter {
    enum ParamType {
        kUBO,
        kSSBO,
        kSampledImage,
        kStorageImage,
    };

    uint32_t binding;
    ParamType type;
    VkShaderStageFlags shader_stages;
};

class DescriptorLayout : public helpers::VulkanObject<DescriptorLayout> {
public:
    ~DescriptorLayout();

    operator VkDescriptorSetLayout() const { return layout; }

public:
    static ptr create(SurfaceDevice::ptr device, const std::vector<DescParameter>& params);

public:
    VkDescriptorSetLayout layout;

private:
    SurfaceDevice::ptr device_;

public:
    DescriptorLayout(PrivateToken, SurfaceDevice::ptr device, VkDescriptorSetLayout layout)
        : device_(device), layout(layout) {}
};

class DescriptorAllocator : public helpers::NonCopyable {
public:
    struct PoolSizeRatio {
        VkDescriptorType type;
        float ratio;
    };

public:
    DescriptorAllocator(SurfaceDevice::ptr device, size_t set_size,
                        std::vector<PoolSizeRatio> ratios);
    ~DescriptorAllocator();

    DescriptorAllocator(DescriptorAllocator&&) noexcept;
    DescriptorAllocator& operator=(DescriptorAllocator&&) noexcept;

public:
    DescriptorUpdater allocate(VkDescriptorSetLayout layout);

    void clear();

private:
    VkDescriptorPool next_pool();

private:
    SurfaceDevice::ptr device_;

    std::vector<PoolSizeRatio> ratios_;
    size_t set_size_{};

    std::vector<VkDescriptorPool> full_pools_;
    std::vector<VkDescriptorPool> active_pools_;
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

    std::vector<DescriptorLayout::ptr> descriptor_layouts;

private:
    SurfaceDevice::ptr surface_device_;
    SwapChain::ptr swap_chain_;
    RenderPass::ptr render_pass_;

public:
    GraphicsPipeline(PrivateToken, SurfaceDevice::ptr surface_device, SwapChain::ptr swap_chain,
                     RenderPass::ptr render_pass, VkPipelineLayout pipeline_layout,
                     VkPipeline graphics_pipeline,
                     std::vector<DescriptorLayout::ptr> descriptor_layouts)
        : surface_device_(surface_device),
          swap_chain_(swap_chain),
          render_pass_(render_pass),
          pipeline_layout(pipeline_layout),
          graphics_pipeline(graphics_pipeline),
          descriptor_layouts(std::move(descriptor_layouts)) {}
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

    GraphicsPipelineBuilder& add_global_layout(DescriptorLayout::ptr layout);

    GraphicsPipelineBuilder& add_local_layout(const std::vector<DescParameter>& params);

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

    std::vector<DescriptorLayout::ptr> descriptor_layouts_;

    bool depth_testing_ = false;
};

class DepthBuffer : public helpers::VulkanObject<DepthBuffer> {
public:
    ~DepthBuffer();

public:
    static VkFormat default_format(SurfaceDevice::ptr surface_device);

    static ptr create(SurfaceDevice::ptr surface_device, size_t w, size_t h, VkFormat format);

    static ptr create(SurfaceDevice::ptr surface_device, size_t w, size_t h);

public:
    helpers::ImageView image_view() const { return helpers::ImageView{image, view, width, height}; }

public:
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;

    size_t width, height;

private:
    SurfaceDevice::ptr surface_device_;

public:
    DepthBuffer(PrivateToken, SurfaceDevice::ptr surface_device, VkImage image, VkImageView view,
                VkDeviceMemory memory, size_t width, size_t height)
        : surface_device_(surface_device),
          image(image),
          view(view),
          memory(memory),
          width(width),
          height(height) {}
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

struct start_rendering : helpers::NonCopyable {
public:
    explicit start_rendering(CommandBuffer::ptr command_buffer, VkRect2D area,
                             const helpers::ImageView& color_attachment,
                             const helpers::ImageView& depth_attachment,
                             const glm::vec4& clear_color = glm::vec4(0.0));

    ~start_rendering();

public:
    start_rendering(start_rendering&&) noexcept;
    start_rendering& operator=(start_rendering&&) noexcept;

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