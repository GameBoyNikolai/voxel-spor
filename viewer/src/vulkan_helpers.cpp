#include "viewer/vulkan_helpers.h"

#include <algorithm>
#include <iostream>

#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"
#include "fmt/format.h"
#include "vulkan/vk_enum_string_helper.h"

namespace spor::vk::helpers {

void check_vulkan(VkResult result) {
    if (result != VK_SUCCESS) {
        std::cerr << fmt::format("Vulkan Error: {}", string_VkResult(result)) << std::endl;
        throw std::runtime_error(fmt::format("Vulkan Error: %s", string_VkResult(result)));
    }
}

VulkanQueueIndices get_device_queues(VkPhysicalDevice device, VkSurfaceKHR surface) {
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_families(family_count);

    vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, queue_families.data());

    VulkanQueueIndices queues;
    for (uint32_t queue_index = 0; queue_index < queue_families.size(); ++queue_index) {
        if (queue_families[queue_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queues.graphics_family = queue_index;
        }

        VkBool32 present_support = false;
        helpers::check_vulkan(
            vkGetPhysicalDeviceSurfaceSupportKHR(device, queue_index, surface, &present_support));
        if (present_support) {
            queues.present_family = queue_index;
        }
    }

    return queues;
}

bool device_supports_extensions(VkPhysicalDevice device,
                                const std::set<std::string>& required_extensions) {
    uint32_t count;
    helpers::check_vulkan(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr));

    std::vector<VkExtensionProperties> available_extensions(count);
    helpers::check_vulkan(
        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available_extensions.data()));

    auto remaining_extensions = required_extensions;
    for (const auto& extension : available_extensions) {
        remaining_extensions.erase(extension.extensionName);
    }

    return remaining_extensions.empty();
}

VulkanSwapChainDetails get_swap_chain_support(VkPhysicalDevice device, VkSurfaceKHR surface) {
    VulkanSwapChainDetails details;

    helpers::check_vulkan(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities));

    uint32_t format_count;
    helpers::check_vulkan(
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr));
    if (format_count > 0) {
        details.formats.resize(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count,
                                             details.formats.data());
    }

    uint32_t present_modes_count;
    check_vulkan(
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_modes_count, nullptr));
    if (present_modes_count > 0) {
        details.present_modes.resize(present_modes_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_modes_count,
                                                  details.present_modes.data());
    }

    return details;
}

size_t device_score(VkPhysicalDevice device, VkSurfaceKHR surface,
                    const std::set<std::string>& required_extensions) {
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceFeatures features;

    vkGetPhysicalDeviceProperties(device, &properties);
    vkGetPhysicalDeviceFeatures(device, &features);

    // Application can't function without geometry shaders
    if (!features.geometryShader) {
        return 0;
    }

    if (!features.samplerAnisotropy) {
        return 0;
    }

    if (!get_device_queues(device, surface).valid()) {
        return 0;
    }

    if (!device_supports_extensions(device, required_extensions)) {
        return 0;
    }

    // Note: only query swap chain support if the swap chain extension is present
    auto swap_chain_support = get_swap_chain_support(device, surface);
    if (swap_chain_support.formats.empty() || swap_chain_support.present_modes.empty()) {
        return 0;
    }

    size_t score = 0;

    // Discrete GPUs have a significant performance advantage
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    }

    // Maximum possible size of textures affects graphics quality
    score += properties.limits.maxImageDimension2D;

    return score;
}

VkPhysicalDevice choose_physical_device(VkInstance instance, VkSurfaceKHR surface,
                                        const std::set<std::string>& required_extensions) {
    uint32_t count = 0;
    check_vulkan(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (count == 0) {
        throw std::runtime_error("Vulkan Error: no devices found");
    }

    std::vector<VkPhysicalDevice> devices(count);
    check_vulkan(vkEnumeratePhysicalDevices(instance, &count, devices.data()));

    std::sort(devices.begin(), devices.end(),
              [surface, &required_extensions](const auto& lhs, const auto& rhs) {
                  return device_score(lhs, surface, required_extensions)
                         < device_score(rhs, surface, required_extensions);
              });

    if (device_score(devices.back(), surface, required_extensions) > 0) {
        return devices.back();
    } else {
        throw std::runtime_error("Vulkan Error: no suitable devices found");
    }
}

VkSurfaceFormatKHR choose_swap_surface_format(
    const std::vector<VkSurfaceFormatKHR>& available_formats) {
    if (available_formats.empty()) {
        throw std::runtime_error("Vulkan Error: no surface formats found");
    }

    for (const auto& available_format : available_formats) {
        if (available_format.format == VK_FORMAT_B8G8R8A8_SRGB
            && available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return available_format;
        }
    }

    return available_formats.front();
}

VkPresentModeKHR choose_swap_present_mode(
    const std::vector<VkPresentModeKHR>& available_present_modes) {
    for (const auto& available_present_mode : available_present_modes) {
        if (available_present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return available_present_mode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;  // always guaranteed to be present
}

VkExtent2D choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t w,
                              uint32_t h) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D actual_extent{w, h};

        actual_extent.width = std::clamp(actual_extent.width, capabilities.minImageExtent.width,
                                         capabilities.maxImageExtent.width);
        actual_extent.height = std::clamp(actual_extent.height, capabilities.minImageExtent.height,
                                          capabilities.maxImageExtent.height);

        return actual_extent;
    }
}

VkRenderPass create_render_pass(VkDevice device, VkFormat format) {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;

    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref{};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;

    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    VkRenderPass render_pass;
    check_vulkan(
        vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass));

    return render_pass;
}

}  // namespace spor::vk::helpers