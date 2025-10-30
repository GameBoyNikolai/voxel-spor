#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "vulkan/vulkan.h"

struct SDL_Window;

namespace spor::vk {

namespace helpers {
void check_vulkan(VkResult result);
}

struct NonCopyable {
    NonCopyable() = default;
    ~NonCopyable() = default;

    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;

    NonCopyable(NonCopyable&&) = default;
    NonCopyable& operator=(NonCopyable&&) = default;
};

struct VulkanQueueIndices {
    std::optional<uint32_t> graphics_family;
    std::optional<uint32_t> present_family;

    bool valid() { return graphics_family.has_value() && present_family.has_value(); }

    size_t count() { return (graphics_family ? 1 : 0) + (present_family ? 1 : 0); }
};

struct VulkanQueues {
    std::optional<VkQueue> graphics_queue;
    std::optional<VkQueue> present_queue;

    VulkanQueues() = default;
    VulkanQueues(const VulkanQueueIndices& indices, VkDevice device) {
        if (indices.graphics_family) {
            graphics_queue.emplace();
            vkGetDeviceQueue(device, *indices.graphics_family, 0, &graphics_queue.value());
        }

        if (indices.present_family) {
            present_queue.emplace();
            vkGetDeviceQueue(device, *indices.present_family, 0, &present_queue.value());
        }
    }
};

template <typename T> struct VulkanObject : public NonCopyable,
                                            public std::enable_shared_from_this<T> {
public:
    using ptr = std::shared_ptr<T>;

protected:
    struct PrivateToken {};
};

class Instance : public VulkanObject<Instance> {
public:
    ~Instance();

public:
    operator VkInstance() const { return instance; }

    static ptr create(const std::string& app_name);

public:
    VkInstance instance;

public:
    Instance(PrivateToken, VkInstance inst) : instance(inst) {}
};

class WindowHandle {
public:
    WindowHandle(SDL_Window* window);
    ~WindowHandle();

    WindowHandle(WindowHandle&&) noexcept;
    WindowHandle& operator=(WindowHandle&&) noexcept;

    WindowHandle(const WindowHandle&) = delete;
    WindowHandle& operator=(const WindowHandle&) = delete;

    operator SDL_Window*();
    operator const SDL_Window*() const;

private:
    SDL_Window* window_;
};

class SurfaceDevice : public VulkanObject<SurfaceDevice> {
public:
    ~SurfaceDevice();

public:
    static ptr create(Instance::ptr inst, std::shared_ptr<WindowHandle> window,
                      const std::set<std::string>& required_extensions);

public:
    VkPhysicalDevice physical_device;
    VkSurfaceKHR surface;
    VkDevice device;

    VulkanQueueIndices indices;
    VulkanQueues queues;

public:
    SurfaceDevice(PrivateToken, std::shared_ptr<Instance> instance,
                  std::shared_ptr<WindowHandle> window, VkPhysicalDevice p_device,
                  VkSurfaceKHR surface, VkDevice device, VulkanQueueIndices indices,
                  VulkanQueues queues)
        : instance_(instance),
          window_(window),
          physical_device(p_device),
          surface(surface),
          device(device),
          indices(indices),
          queues(queues) {}

private:
    std::shared_ptr<Instance> instance_;
    std::shared_ptr<WindowHandle> window_;
};

class SwapChain : public VulkanObject<SwapChain> {
public:
    ~SwapChain();

public:
    static ptr create(SurfaceDevice::ptr surface_device, uint32_t w, uint32_t h);

public:
    VkSwapchainKHR swap_chain;
    std::vector<VkImage> images;
    std::vector<VkImageView> swap_chain_views;

    VkFormat format;
    VkExtent2D extent;

private:
    SurfaceDevice::ptr surface_device_;

public:
    SwapChain(PrivateToken, SurfaceDevice::ptr surface_device, VkSwapchainKHR swap_chain,
              std::vector<VkImage> images, std::vector<VkImageView> swap_chain_views,
              VkFormat format, VkExtent2D extent)
        : surface_device_(surface_device),
          swap_chain(swap_chain),
          images(images),
          swap_chain_views(swap_chain_views),
          format(format),
          extent(extent) {}
};

class GraphicsPipeline : public VulkanObject<GraphicsPipeline> {
public:
    ~GraphicsPipeline();

public:
    friend class GraphicsPipelineBuilder;

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

    GraphicsPipelineBuilder& add_descriptor_set(
        VkDescriptorSetLayout descriptor_set);

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

class CommandPool : public VulkanObject<CommandPool> {
public:
    ~CommandPool();

public:
    static ptr create(SurfaceDevice::ptr surface_device);

public:
    VkCommandPool command_pool;

private:
    SurfaceDevice::ptr surface_device_;

public:
    CommandPool(PrivateToken, SurfaceDevice::ptr surface_device, VkCommandPool pool)
        : surface_device_(surface_device), command_pool(pool) {}
};

class CommandBuffer : public VulkanObject<CommandBuffer> {
public:
    VkCommandBuffer command_buffer;

public:
    static ptr create(SurfaceDevice::ptr surface_device, CommandPool::ptr command_pool);

private:
    SurfaceDevice::ptr surface_device_;

public:
    CommandBuffer(PrivateToken, SurfaceDevice::ptr surface_device, VkCommandBuffer buffer)
        : surface_device_(surface_device), command_buffer(buffer) {}
};

class SwapChainFramebuffers : public VulkanObject<SwapChainFramebuffers> {
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

class record_commands : NonCopyable {
public:
    explicit record_commands(CommandBuffer::ptr command_buffer);

    ~record_commands();

public:
    record_commands(record_commands&&) noexcept;
    record_commands& operator=(record_commands&&) noexcept;

private:
    CommandBuffer::ptr command_buffer_;
};

class render_pass : NonCopyable {
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

struct DefaultRenderSyncObjects : public VulkanObject<DefaultRenderSyncObjects> {
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