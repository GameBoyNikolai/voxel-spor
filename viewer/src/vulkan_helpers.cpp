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

bool VulkanDeviceCapabilities::valid(const std::set<std::string>& required_extensions) {
    auto remaining_extensions = required_extensions;
    for (const auto& extension : available_extensions) {
        remaining_extensions.erase(extension.extensionName);
    }

    bool all_extensions_present = remaining_extensions.empty();

    return !graphics_queues.empty()           //
           && !present_queues.empty()         //
           && !compute_queues.empty()         //
           && !surface_formats.empty()        //
           && !present_modes.empty()          //
           && all_extensions_present          //
           && device_features.geometryShader  //
           && device_features.samplerAnisotropy;
}

VulkanDeviceCapabilities get_full_device_capabilities(VkPhysicalDevice device,
                                                      VkSurfaceKHR surface) {
    VulkanDeviceCapabilities capabilities{};

    // queues
    {
        uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, nullptr);

        std::vector<VkQueueFamilyProperties> queue_families(family_count);

        vkGetPhysicalDeviceQueueFamilyProperties(device, &family_count, queue_families.data());

        VulkanQueueIndices queues;
        for (uint32_t queue_index = 0; queue_index < queue_families.size(); ++queue_index) {
            if (queue_families[queue_index].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                capabilities.graphics_queues.insert(queue_index);
            }
            if (queue_families[queue_index].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                capabilities.compute_queues.insert(queue_index);
            }

            VkBool32 present_support = false;
            helpers::check_vulkan(vkGetPhysicalDeviceSurfaceSupportKHR(device, queue_index, surface,
                                                                       &present_support));
            if (present_support) {
                capabilities.present_queues.insert(queue_index);
            }
        }
    }

    // swap chain
    {
        helpers::check_vulkan(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            device, surface, &capabilities.surface_capabilities));

        uint32_t format_count;
        helpers::check_vulkan(
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr));
        if (format_count > 0) {
            capabilities.surface_formats.resize(format_count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count,
                                                 capabilities.surface_formats.data());
        }

        uint32_t present_modes_count;
        check_vulkan(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface,
                                                               &present_modes_count, nullptr));
        if (present_modes_count > 0) {
            capabilities.present_modes.resize(present_modes_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_modes_count,
                                                      capabilities.present_modes.data());
        }
    }

    // extensions
    {
        uint32_t count;
        helpers::check_vulkan(
            vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr));

        capabilities.available_extensions.resize(count);
        helpers::check_vulkan(vkEnumerateDeviceExtensionProperties(
            device, nullptr, &count, capabilities.available_extensions.data()));
    }

    // device info
    {
        vkGetPhysicalDeviceProperties(device, &capabilities.device_properties);
        vkGetPhysicalDeviceFeatures(device, &capabilities.device_features);
    }

    // msaa
    {
        VkSampleCountFlags counts
            = capabilities.device_properties.limits.framebufferColorSampleCounts
              & capabilities.device_properties.limits.framebufferDepthSampleCounts;

        capabilities.max_msaa_samples = VK_SAMPLE_COUNT_1_BIT;

        if (counts & VK_SAMPLE_COUNT_2_BIT) {
            capabilities.max_msaa_samples = VK_SAMPLE_COUNT_2_BIT;
        }
        if (counts & VK_SAMPLE_COUNT_4_BIT) {
            capabilities.max_msaa_samples = VK_SAMPLE_COUNT_4_BIT;
        }
        if (counts & VK_SAMPLE_COUNT_8_BIT) {
            capabilities.max_msaa_samples = VK_SAMPLE_COUNT_8_BIT;
        }
        if (counts & VK_SAMPLE_COUNT_16_BIT) {
            capabilities.max_msaa_samples = VK_SAMPLE_COUNT_16_BIT;
        }
        if (counts & VK_SAMPLE_COUNT_32_BIT) {
            capabilities.max_msaa_samples = VK_SAMPLE_COUNT_32_BIT;
        }
        if (counts & VK_SAMPLE_COUNT_64_BIT) {
            capabilities.max_msaa_samples = VK_SAMPLE_COUNT_64_BIT;
        }
    }

    return capabilities;
}

size_t device_score(VkPhysicalDevice device, VkSurfaceKHR surface,
                    const std::set<std::string>& required_extensions) {
    auto device_caps = get_full_device_capabilities(device, surface);
    if (!device_caps.valid(required_extensions)) {
        return 0;
    }

    size_t score = 0;

    // Discrete GPUs have a significant performance advantage
    if (device_caps.device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    }

    // Maximum possible size of textures affects graphics quality
    score += device_caps.device_properties.limits.maxImageDimension2D;

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

VkRenderPass create_render_pass(VkDevice device, VkFormat color_format,
                                std::optional<VkFormat> depth_format) {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = color_format;
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

    VkAttachmentDescription depth_attachment{};
    VkAttachmentReference depth_attachment_ref{};
    if (depth_format) {
        depth_attachment.format = *depth_format;
        depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        depth_attachment_ref.attachment = 1;
        depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    if (depth_format) {
        subpass.pDepthStencilAttachment = &depth_attachment_ref;
    }

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;

    dependency.srcStageMask
        = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                              | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask
        = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::vector<VkAttachmentDescription> attachments = {color_attachment};
    if (depth_format) {
        attachments.push_back(depth_attachment);
    }

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = static_cast<uint32_t>(attachments.size());
    render_pass_info.pAttachments = attachments.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    VkRenderPass render_pass;
    check_vulkan(vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass));

    return render_pass;
}

uint32_t choose_memory_type(VkPhysicalDevice p_device, uint32_t type_filter,
                            VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_properties;
    vkGetPhysicalDeviceMemoryProperties(p_device, &mem_properties);

    for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
        if ((type_filter & (1 << i))
            && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Unable to find a suitable buffer memory type");
}

VkFormat choose_supported_format(VkPhysicalDevice p_device, const std::vector<VkFormat>& candidates,
                                 VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(p_device, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR
            && (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL
                   && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format!");
}

bool has_stencil_component(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

std::pair<VkImage, VkDeviceMemory> create_image(VkDevice device, VkPhysicalDevice p_device,
                                                size_t width, size_t height, VkFormat format,
                                                VkImageTiling tiling, VkImageUsageFlags usage,
                                                VkMemoryPropertyFlags properties) {
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = static_cast<uint32_t>(width);
    image_info.extent.height = static_cast<uint32_t>(height);
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;

    image_info.format = format;
    image_info.tiling = tiling;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    image_info.usage = usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.flags = 0;  // Optional

    VkImage image;
    check_vulkan(vkCreateImage(device, &image_info, nullptr, &image));

    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(device, image, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex
        = helpers::choose_memory_type(p_device, mem_requirements.memoryTypeBits, properties);

    VkDeviceMemory memory;
    helpers::check_vulkan(vkAllocateMemory(device, &alloc_info, nullptr, &memory));

    helpers::check_vulkan(vkBindImageMemory(device, image, memory, 0));

    return {image, memory};
}

VkImageView create_image_view(VkDevice device, VkImage image, VkFormat format,
                              VkImageAspectFlags aspect) {
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspect;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    VkImageView view;
    check_vulkan(vkCreateImageView(device, &view_info, nullptr, &view));

    return view;
}

}  // namespace spor::vk::helpers