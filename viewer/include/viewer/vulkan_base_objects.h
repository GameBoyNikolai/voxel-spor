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

    helpers::VulkanQueueIndices indices;
    helpers::VulkanQueues queues;

public:
    SurfaceDevice(PrivateToken, std::shared_ptr<Instance> instance,
                  std::shared_ptr<WindowHandle> window, VkPhysicalDevice p_device,
                  VkSurfaceKHR surface, VkDevice device, helpers::VulkanQueueIndices indices,
                  helpers::VulkanQueues queues)
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

}  // namespace spor::vk