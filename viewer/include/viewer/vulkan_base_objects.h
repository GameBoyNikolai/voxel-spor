#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "viewer/vulkan_helpers.h"
#include "vulkan/vulkan.h"

struct SDL_Window;

namespace spor::vk {

class Instance : public helpers::VulkanObject<Instance> {
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

struct VulkanQueueInfo {
    struct QueueBundle {
        uint32_t index;
        VkQueue queue;
        VkQueueFlags type;
    };

    QueueBundle graphics;  // compute-enabled graphics queue
    QueueBundle present;

    std::optional<QueueBundle> compute;
};

class SurfaceDevice : public helpers::VulkanObject<SurfaceDevice> {
public:
    ~SurfaceDevice();

public:
    operator VkPhysicalDevice() const { return physical_device; }
    operator VkDevice() const { return device; }

    static ptr create(Instance::ptr inst, std::shared_ptr<WindowHandle> window,
                      const std::set<std::string>& required_extensions);

public:
    VkPhysicalDevice physical_device;
    VkSurfaceKHR surface;
    VkDevice device;

    VulkanQueueInfo queues;

    helpers::VulkanDeviceCapabilities capabilities;

public:
    SurfaceDevice(PrivateToken, std::shared_ptr<Instance> instance,
                  std::shared_ptr<WindowHandle> window, VkPhysicalDevice p_device,
                  VkSurfaceKHR surface, VkDevice device, VulkanQueueInfo queues,
                  helpers::VulkanDeviceCapabilities capabilities)
        : instance_(instance),
          window_(window),
          physical_device(p_device),
          surface(surface),
          device(device),
          queues(queues),
          capabilities(std::move(capabilities)) {}

private:
    std::shared_ptr<Instance> instance_;
    std::shared_ptr<WindowHandle> window_;
};

class SwapChain : public helpers::VulkanObject<SwapChain> {
public:
    ~SwapChain();

public:
    operator VkSwapchainKHR const() { return swap_chain; }

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

class CommandPool : public helpers::VulkanObject<CommandPool> {
public:
    ~CommandPool();

public:
    operator VkCommandPool() const { return command_pool; }

    static ptr create(SurfaceDevice::ptr surface_device, VulkanQueueInfo::QueueBundle queue);

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

class record_commands : helpers::NonCopyable {
public:
    explicit record_commands(CommandBuffer::ptr command_buffer, bool reset = true);

    ~record_commands();

public:
    record_commands(record_commands&&) noexcept;
    record_commands& operator=(record_commands&&) noexcept;

private:
    CommandBuffer::ptr command_buffer_;
};

}  // namespace spor::vk