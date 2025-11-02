#include "viewer/vulkan_base_objects.h"

#include <algorithm>
#include <iostream>

#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"
#include "fmt/format.h"
#include "vulkan/vk_enum_string_helper.h"

namespace spor::vk {

namespace helpers {

void check_sdl(int code) {
    if (code < 0) {
        const char* err = SDL_GetError();
        std::cerr << fmt::format("SDL Error: {}", err) << std::endl;
        throw std::runtime_error(fmt::format("SDL Error: %s", err));
    }
}

}  // namespace helpers

Instance::~Instance() { vkDestroyInstance(instance, nullptr); }

Instance::ptr Instance::create(const std::string& app_name) {
    VkApplicationInfo app_info{};

    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = app_name.data();
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledLayerCount = 0;

    create_info.ppEnabledExtensionNames
        = SDL_Vulkan_GetInstanceExtensions(&create_info.enabledExtensionCount);
    if (!create_info.ppEnabledExtensionNames) {
        helpers::check_sdl(-1);
    }

    VkInstance inst;
    helpers::check_vulkan(vkCreateInstance(&create_info, nullptr, &inst));

    return std::make_shared<Instance>(PrivateToken{}, inst);
}

WindowHandle::WindowHandle(SDL_Window* window) : window_(window) {}

WindowHandle::~WindowHandle() {
    if (window_) {
        SDL_DestroyWindow(window_);
    }
}

WindowHandle::WindowHandle(WindowHandle&& other) noexcept { *this = std::move(other); }

WindowHandle& WindowHandle::operator=(WindowHandle&& other) noexcept {
    std::swap(window_, other.window_);

    return *this;
}

WindowHandle::operator SDL_Window*() { return window_; }

WindowHandle::operator const SDL_Window*() const { return window_; }

SurfaceDevice::~SurfaceDevice() {
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(*instance_, surface, nullptr);
}

SurfaceDevice::ptr SurfaceDevice::create(Instance::ptr inst, std::shared_ptr<WindowHandle> window,
                                         const std::set<std::string>& required_extensions) {
    VkSurfaceKHR surface;
    if (!SDL_Vulkan_CreateSurface(*window, *inst, nullptr, &surface)) {
        helpers::check_sdl(-1);
    }

    VkPhysicalDevice physical_device
        = helpers::choose_physical_device(*inst, surface, required_extensions);

    helpers::VulkanQueueIndices indices = helpers::get_device_queues(physical_device, surface);

    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;

    // our chosen physical device was guaranteed to have both of these queues, but might be good
    // to validate that later
    float priority = 1.0f;
    for (uint32_t family : std::set<uint32_t>({
             *indices.graphics_family,
             *indices.present_family,
         })) {
        auto& queue_create_info = queue_create_infos.emplace_back();
        queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_info.queueFamilyIndex = family;
        queue_create_info.queueCount = 1;

        queue_create_info.pQueuePriorities = &priority;
    }

    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.pQueueCreateInfos = queue_create_infos.data();
    create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());

    create_info.pEnabledFeatures = &features;

    std::vector<const char*> extensions(required_extensions.size());
    std::transform(required_extensions.begin(), required_extensions.end(), extensions.begin(),
                   [](const auto& ext) { return ext.data(); });

    create_info.ppEnabledExtensionNames = extensions.data();
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());

    create_info.enabledLayerCount = 0;

    VkDevice logical_device;
    helpers::check_vulkan(vkCreateDevice(physical_device, &create_info, nullptr, &logical_device));

    helpers::VulkanQueues queues(indices, logical_device);

    return std::make_shared<SurfaceDevice>(PrivateToken{}, inst, window, physical_device, surface,
                                           logical_device, indices, queues);
}

SwapChain::~SwapChain() {
    for (const auto& image_view : swap_chain_views) {
        vkDestroyImageView(*surface_device_, image_view, nullptr);
    }
    vkDestroySwapchainKHR(*surface_device_, swap_chain, nullptr);
}

SwapChain::ptr SwapChain::create(SurfaceDevice::ptr surface_device, uint32_t w, uint32_t h) {
    auto swap_chain_details
        = helpers::get_swap_chain_support(surface_device->physical_device, surface_device->surface);

    auto format = helpers::choose_swap_surface_format(swap_chain_details.formats);
    auto present_mode = helpers::choose_swap_present_mode(swap_chain_details.present_modes);
    auto extent = helpers::choose_swap_extent(swap_chain_details.capabilities, w, h);

    uint32_t image_count = swap_chain_details.capabilities.minImageCount + 1;
    if (swap_chain_details.capabilities.maxImageCount > 0
        && image_count > swap_chain_details.capabilities.maxImageCount) {
        image_count = swap_chain_details.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR chain_create_info{};
    chain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    chain_create_info.surface = surface_device->surface;

    chain_create_info.minImageCount = image_count;
    chain_create_info.imageFormat = format.format;
    chain_create_info.imageColorSpace = format.colorSpace;
    chain_create_info.imageExtent = extent;
    chain_create_info.imageArrayLayers = 1;
    chain_create_info.imageUsage
        = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;  // note: this will be
                                                // VK_IMAGE_USAGE_TRANSFER_DST_BIT for images
                                                // involved in off screen rendering?

    // at this point, we have ensured validity of the chosen device's queues
    auto indices = surface_device->indices;
    uint32_t queueFamilyIndices[]
        = {indices.graphics_family.value(), indices.present_family.value()};

    if (indices.graphics_family != indices.present_family) {
        chain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        chain_create_info.queueFamilyIndexCount = 2;
        chain_create_info.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        chain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        chain_create_info.queueFamilyIndexCount = 0;      // Optional
        chain_create_info.pQueueFamilyIndices = nullptr;  // Optional
    }

    chain_create_info.preTransform = swap_chain_details.capabilities.currentTransform;

    chain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;  // for blending windows

    chain_create_info.presentMode = present_mode;
    chain_create_info.clipped = VK_TRUE;

    chain_create_info.oldSwapchain = VK_NULL_HANDLE;

    VkSwapchainKHR swap_chain;
    helpers::check_vulkan(
        vkCreateSwapchainKHR(*surface_device, &chain_create_info, nullptr, &swap_chain));

    helpers::check_vulkan(
        vkGetSwapchainImagesKHR(*surface_device, swap_chain, &image_count, nullptr));

    std::vector<VkImage> swap_chain_images(image_count);
    helpers::check_vulkan(vkGetSwapchainImagesKHR(*surface_device, swap_chain, &image_count,
                                                  swap_chain_images.data()));

    std::vector<VkImageView> swap_chain_views(image_count);

    for (size_t i = 0; i < swap_chain_views.size(); ++i) {
        VkImageViewCreateInfo view_create_info{};
        view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_create_info.image = swap_chain_images[i];
        view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_create_info.format = format.format;

        view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_create_info.subresourceRange.baseMipLevel = 0;
        view_create_info.subresourceRange.levelCount = 1;
        view_create_info.subresourceRange.baseArrayLayer = 0;
        view_create_info.subresourceRange.layerCount = 1;

        helpers::check_vulkan(vkCreateImageView(*surface_device, &view_create_info, nullptr,
                                                &swap_chain_views[i]));
    }

    return std::make_shared<SwapChain>(PrivateToken{}, surface_device, swap_chain,
                                       swap_chain_images, swap_chain_views, format.format, extent);
}

}  // namespace spor::vk