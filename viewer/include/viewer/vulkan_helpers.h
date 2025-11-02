#pragma once

#include <memory>
#include <optional>
#include <string>
#include <set>
#include <vector>

#include "vulkan/vulkan.h"

namespace spor::vk::helpers {

struct NonCopyable {
    NonCopyable() = default;
    ~NonCopyable() = default;

    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;

    NonCopyable(NonCopyable&&) = default;
    NonCopyable& operator=(NonCopyable&&) = default;
};

template <typename T> struct VulkanObject : public NonCopyable,
                                            public std::enable_shared_from_this<T> {
public:
    using ptr = std::shared_ptr<T>;

protected:
    struct PrivateToken {};
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

struct VulkanSwapChainDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

void check_vulkan(VkResult result);

VulkanQueueIndices get_device_queues(VkPhysicalDevice device, VkSurfaceKHR surface);

bool device_supports_extensions(VkPhysicalDevice device,
                                const std::set<std::string>& required_extensions);

VulkanSwapChainDetails get_swap_chain_support(VkPhysicalDevice device, VkSurfaceKHR surface);

VkPhysicalDevice choose_physical_device(VkInstance instance, VkSurfaceKHR surface,
                                        const std::set<std::string>& required_extensions);

VkSurfaceFormatKHR choose_swap_surface_format(
    const std::vector<VkSurfaceFormatKHR>& available_formats);

VkPresentModeKHR choose_swap_present_mode(
    const std::vector<VkPresentModeKHR>& available_present_modes);

VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t w, uint32_t h);

VkRenderPass create_render_pass(VkDevice device, VkFormat format);

}  // namespace spor::vk::helpers