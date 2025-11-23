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

struct VulkanDeviceCapabilities {
    std::set<uint32_t> graphics_queues;
    std::set<uint32_t> present_queues;
    std::set<uint32_t> compute_queues;

    VkSurfaceCapabilitiesKHR surface_capabilities;
    std::vector<VkSurfaceFormatKHR> surface_formats;
    std::vector<VkPresentModeKHR> present_modes;

    VkPhysicalDeviceProperties device_properties;
    VkPhysicalDeviceFeatures device_features;

    std::vector<VkExtensionProperties> available_extensions;

    VkSampleCountFlagBits max_msaa_samples;

    bool valid(const std::set<std::string>& required_extensions);
};

struct ImageView {
    VkImage image;
    VkImageView view;

    size_t w, h;
};

void check_vulkan(VkResult result);

VulkanDeviceCapabilities get_full_device_capabilities(VkPhysicalDevice device,
                                                      VkSurfaceKHR surface);

VkPhysicalDevice choose_physical_device(VkInstance instance, VkSurfaceKHR surface,
                                        const std::set<std::string>& required_extensions);

VkSurfaceFormatKHR choose_swap_surface_format(
    const std::vector<VkSurfaceFormatKHR>& available_formats);

VkPresentModeKHR choose_swap_present_mode(
    const std::vector<VkPresentModeKHR>& available_present_modes);

VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t w, uint32_t h);

VkRenderPass create_render_pass(VkDevice device, VkFormat color_format, std::optional<VkFormat> depth_format = std::nullopt);

uint32_t choose_memory_type(VkPhysicalDevice p_device, uint32_t type_filter,
                            VkMemoryPropertyFlags properties);

VkFormat choose_supported_format(VkPhysicalDevice p_device, const std::vector<VkFormat>& candidates,
                                 VkImageTiling tiling, VkFormatFeatureFlags features);

bool has_stencil_component(VkFormat format);

std::pair<VkImage, VkDeviceMemory> create_image(VkDevice device, VkPhysicalDevice p_device,
                                                size_t width, size_t height, VkFormat format,
                                                VkImageTiling tiling, VkImageUsageFlags usage,
                                                VkMemoryPropertyFlags properties);

VkImageView create_image_view(VkDevice device, VkImage image, VkFormat format,
                              VkImageAspectFlags aspect);

}  // namespace spor::vk::helpers