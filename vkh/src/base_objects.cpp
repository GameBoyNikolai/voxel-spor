#include "vkh/base_objects.h"

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

    // If a physical device was chosen without an exception, the capabilities are guaranteed to be
    // valid
    VkPhysicalDevice physical_device
        = helpers::choose_physical_device(*inst, surface, required_extensions);
    auto device_capabilities = helpers::get_full_device_capabilities(physical_device, surface);

    std::set<uint32_t> gcomp_queues;
    std::set_intersection(
        device_capabilities.graphics_queues.begin(), device_capabilities.graphics_queues.end(),
        device_capabilities.compute_queues.begin(), device_capabilities.compute_queues.end(),
        std::inserter(gcomp_queues, gcomp_queues.end()));

    std::set<uint32_t> comp_only_queues;
    std::set_difference(
        device_capabilities.compute_queues.begin(), device_capabilities.compute_queues.end(),
        device_capabilities.graphics_queues.begin(), device_capabilities.graphics_queues.end(),
        std::inserter(gcomp_queues, gcomp_queues.end()));

    if (gcomp_queues.empty()) {
        throw std::runtime_error("No queue exists that supports both graphics and compute");
    }

    uint32_t gcomp_index = *gcomp_queues.begin();
    uint32_t present_index = *device_capabilities.present_queues.begin();
    std::set<uint32_t> all_queues = {gcomp_index, present_index};

    std::optional<uint32_t> comp_only_index;
    if (!comp_only_queues.empty()) {
        comp_only_index = *comp_only_queues.begin();
        all_queues.insert(*comp_only_index);
    }

    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    float priority = 1.0f;
    for (uint32_t family : all_queues) {
        auto& queue_create_info = queue_create_infos.emplace_back();
        queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_info.queueFamilyIndex = family;
        queue_create_info.queueCount = 1;

        queue_create_info.pQueuePriorities = &priority;
    }

    VkPhysicalDeviceFeatures features{};

    VkPhysicalDeviceVulkan13Features features_13{};
    features_13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features_13.pNext = nullptr;
    features_13.synchronization2 = VK_TRUE;
    features_13.dynamicRendering = VK_TRUE;

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.pNext = &features_13;
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

    VulkanQueueInfo queue_info;

    queue_info.graphics.index = gcomp_index;
    queue_info.graphics.type = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    vkGetDeviceQueue(logical_device, queue_info.graphics.index, 0, &queue_info.graphics.queue);

    queue_info.present.index = present_index;
    queue_info.present.type = 0;  // present has no flag :(
    vkGetDeviceQueue(logical_device, queue_info.present.index, 0, &queue_info.present.queue);

    if (comp_only_index) {
        auto& comp_queue = queue_info.compute.emplace();
        comp_queue.index = *comp_only_index;
        comp_queue.type = VK_QUEUE_COMPUTE_BIT;
        vkGetDeviceQueue(logical_device, comp_queue.index, 0, &comp_queue.queue);
    }

    return std::make_shared<SurfaceDevice>(PrivateToken{}, inst, window, physical_device, surface,
                                           logical_device, queue_info,
                                           std::move(device_capabilities));
}

SwapChain::~SwapChain() {
    for (const auto& image_view : swap_chain_views) {
        vkDestroyImageView(*surface_device_, image_view, nullptr);
    }
    vkDestroySwapchainKHR(*surface_device_, swap_chain, nullptr);
}

SwapChain::ptr SwapChain::create(SurfaceDevice::ptr surface_device, uint32_t w, uint32_t h) {
    const auto& device_capabilities = surface_device->capabilities;

    auto format = helpers::choose_swap_surface_format(device_capabilities.surface_formats);
    auto present_mode = helpers::choose_swap_present_mode(device_capabilities.present_modes);
    auto extent = helpers::choose_swap_extent(device_capabilities.surface_capabilities, w, h);

    uint32_t image_count = device_capabilities.surface_capabilities.minImageCount + 1;
    if (device_capabilities.surface_capabilities.maxImageCount > 0
        && image_count > device_capabilities.surface_capabilities.maxImageCount) {
        image_count = device_capabilities.surface_capabilities.maxImageCount;
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
    auto queues = surface_device->queues;
    uint32_t queueFamilyIndices[] = {queues.graphics.index, queues.present.index};
    // TODO: will compute need to be included here?

    if (queues.graphics.index != queues.present.index) {
        chain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        chain_create_info.queueFamilyIndexCount = 2;
        chain_create_info.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        chain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        chain_create_info.queueFamilyIndexCount = 0;      // Optional
        chain_create_info.pQueueFamilyIndices = nullptr;  // Optional
    }

    chain_create_info.preTransform = device_capabilities.surface_capabilities.currentTransform;

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

        helpers::check_vulkan(
            vkCreateImageView(*surface_device, &view_create_info, nullptr, &swap_chain_views[i]));
    }

    return std::make_shared<SwapChain>(PrivateToken{}, surface_device, swap_chain,
                                       swap_chain_images, swap_chain_views, format.format, extent);
}

helpers::ImageView SwapChain::image_view(size_t index) {
    if (index >= images.size()) {
        throw std::out_of_range("Swap Chain frame index out of range");
    }

    return helpers::ImageView{images[index], swap_chain_views[index], extent.width, extent.height};
}

CommandPool::~CommandPool() { vkDestroyCommandPool(*surface_device_, command_pool, nullptr); }

CommandPool::ptr CommandPool::create(SurfaceDevice::ptr surface_device,
                                     VulkanQueueInfo::QueueBundle queue) {
    // TODO: allow command pools to be created on different queues
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue.index;

    VkCommandPool pool;
    helpers::check_vulkan(vkCreateCommandPool(*surface_device, &pool_info, nullptr, &pool));

    return std::make_shared<CommandPool>(PrivateToken{}, surface_device, pool);
}

CommandBuffer::ptr CommandPool::primary_buffer(bool reset_on_fetch) {
    if (!primary_buffer_) {
        primary_buffer_ = CommandBuffer::create(surface_device_, shared_from_this());
    }

    if (reset_on_fetch) {
        helpers::check_vulkan(vkResetCommandBuffer(*primary_buffer_, 0));
    }

    return primary_buffer_;
}

CommandBuffer::ptr CommandBuffer::create(SurfaceDevice::ptr surface_device,
                                         CommandPool::ptr command_pool) {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool->command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer buffer;
    helpers::check_vulkan(vkAllocateCommandBuffers(*surface_device, &alloc_info, &buffer));

    return std::make_shared<CommandBuffer>(PrivateToken{}, surface_device, buffer);
}

record_commands::record_commands(CommandBuffer::ptr command_buffer)
    : command_buffer_(command_buffer) {
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = 0;                   // Optional
    begin_info.pInheritanceInfo = nullptr;  // Optional

    helpers::check_vulkan(vkBeginCommandBuffer(command_buffer->command_buffer, &begin_info));
}

record_commands::~record_commands() {
    if (command_buffer_) {
        helpers::check_vulkan(vkEndCommandBuffer(command_buffer_->command_buffer));
    }
}

record_commands::record_commands(record_commands&& other) noexcept { *this = std::move(other); }

record_commands& record_commands::operator=(record_commands&& other) noexcept {
    std::swap(command_buffer_, other.command_buffer_);

    return *this;
}

void transition_image(CommandBuffer::ptr cmd, const helpers::ImageView& image, VkImageLayout from,
                      VkImageLayout to) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.pNext = nullptr;

    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

    barrier.oldLayout = from;
    barrier.newLayout = to;

    VkImageAspectFlags aspect_mask = (to == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
                                        ? VK_IMAGE_ASPECT_DEPTH_BIT
                                        : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.aspectMask = aspect_mask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    barrier.image = image.image;

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.pNext = nullptr;

    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(*cmd, &depInfo);
}

void blit_image(CommandBuffer::ptr cmd, const helpers::ImageView& src,
                const helpers::ImageView& dst) {
    VkImageBlit2 blit_config{};
    blit_config.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    blit_config.pNext = nullptr;

    blit_config.srcOffsets[1].x = static_cast<int32_t>(src.w);
    blit_config.srcOffsets[1].y = static_cast<int32_t>(src.h);
    blit_config.srcOffsets[1].z = 1;

    blit_config.dstOffsets[1].x = static_cast<int32_t>(dst.w);
    blit_config.dstOffsets[1].y = static_cast<int32_t>(dst.h);
    blit_config.dstOffsets[1].z = 1;

    blit_config.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit_config.srcSubresource.baseArrayLayer = 0;
    blit_config.srcSubresource.layerCount = 1;
    blit_config.srcSubresource.mipLevel = 0;

    blit_config.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit_config.dstSubresource.baseArrayLayer = 0;
    blit_config.dstSubresource.layerCount = 1;
    blit_config.dstSubresource.mipLevel = 0;

    VkBlitImageInfo2 blit_info{};
    blit_info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, blit_info.pNext = nullptr;
    blit_info.dstImage = dst.image;
    blit_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blit_info.srcImage = src.image;
    blit_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blit_info.filter = VK_FILTER_LINEAR;
    blit_info.regionCount = 1;
    blit_info.pRegions = &blit_config;

    vkCmdBlitImage2(*cmd, &blit_info);
}

}  // namespace spor::vk