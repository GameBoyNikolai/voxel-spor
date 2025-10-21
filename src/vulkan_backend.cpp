#include "voxel_spor/vulkan_backend.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"
#include "fmt/format.h"
#include "shaders/test.frag.inl"
#include "shaders/test.vert.inl"
#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vulkan.h"

namespace spor {

void check_sdl(int code) {
    if (code < 0) {
        const char* err = SDL_GetError();
        std::cerr << fmt::format("SDL Error: {}", err) << std::endl;
        throw std::runtime_error(fmt::format("SDL Error: %s", err));
    }
}

void check_vulkan(VkResult result) {
    if (result != VK_SUCCESS) {
        std::cerr << fmt::format("Vulkan Error: {}", string_VkResult(result)) << std::endl;
        throw std::runtime_error(fmt::format("Vulkan Error: %s", string_VkResult(result)));
    }
}

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

struct VulkanSwapChain {
    VkSwapchainKHR handle;
    std::vector<VkImage> images;
    std::vector<VkImageView> views;

    VkFormat format;
    VkExtent2D extent;
};

struct RenderSyncObjects {
    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;

    void create(VkDevice device) {
        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        check_vulkan(vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available));
        check_vulkan(vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished));
        check_vulkan(vkCreateFence(device, &fence_info, nullptr, &in_flight));
    }

    void destroy(VkDevice device) {
        vkDestroyFence(device, in_flight, nullptr);
        vkDestroySemaphore(device, render_finished, nullptr);
        vkDestroySemaphore(device, image_available, nullptr);
    }
};

struct VulkanBackend::ImplState {
public:
    // SDL
    SDL_Window* window{nullptr};

    // Vulkan
    VkInstance vk_inst{VK_NULL_HANDLE};
    VkPhysicalDevice vk_physical_device{VK_NULL_HANDLE};
    VkDevice vk_logical_device{VK_NULL_HANDLE};

    VulkanQueues vk_queues;

    VkSurfaceKHR vk_surface;
    VulkanSwapChain vk_swap_chain;

    VkRenderPass vk_render_pass;
    VkPipelineLayout vk_pipeline_layout;
    VkPipeline vk_graphics_pipeline;

    std::vector<VkFramebuffer> vk_swap_chain_framebuffers;

    VkCommandPool vk_command_pool;
    VkCommandBuffer vk_command_buffer;

    RenderSyncObjects sync_objects;

public:
    void create_vulkan_instance(const std::string& title) {
        VkApplicationInfo app_info{};

        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = title.data();
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
            check_sdl(-1);
        }

        check_vulkan(vkCreateInstance(&create_info, nullptr, &vk_inst));
    }

    VulkanQueueIndices get_device_queues(VkPhysicalDevice device) {
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
            check_vulkan(vkGetPhysicalDeviceSurfaceSupportKHR(device, queue_index, vk_surface,
                                                              &present_support));
            if (present_support) {
                queues.present_family = queue_index;
            }
        }

        return queues;
    }

    bool device_supports_extensions(VkPhysicalDevice device,
                                    const std::set<std::string>& required_extensions) {
        uint32_t count;
        check_vulkan(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr));

        std::vector<VkExtensionProperties> available_extensions(count);
        check_vulkan(vkEnumerateDeviceExtensionProperties(device, nullptr, &count,
                                                          available_extensions.data()));

        auto remaining_extensions = required_extensions;
        for (const auto& extension : available_extensions) {
            remaining_extensions.erase(extension.extensionName);
        }

        return remaining_extensions.empty();
    }

    VulkanSwapChainDetails get_swap_chain_support(VkPhysicalDevice device) {
        VulkanSwapChainDetails details;

        check_vulkan(
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, vk_surface, &details.capabilities));

        uint32_t format_count;
        check_vulkan(
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, vk_surface, &format_count, nullptr));
        if (format_count > 0) {
            details.formats.resize(format_count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, vk_surface, &format_count,
                                                 details.formats.data());
        }

        uint32_t present_modes_count;
        check_vulkan(vkGetPhysicalDeviceSurfacePresentModesKHR(device, vk_surface,
                                                               &present_modes_count, nullptr));
        if (present_modes_count > 0) {
            details.present_modes.resize(present_modes_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, vk_surface, &present_modes_count,
                                                      details.present_modes.data());
        }

        return details;
    }

    size_t device_score(VkPhysicalDevice device, const std::set<std::string>& required_extensions) {
        VkPhysicalDeviceProperties properties;
        VkPhysicalDeviceFeatures features;

        vkGetPhysicalDeviceProperties(device, &properties);
        vkGetPhysicalDeviceFeatures(device, &features);

        // Application can't function without geometry shaders
        if (!features.geometryShader) {
            return 0;
        }

        if (!get_device_queues(device).valid()) {
            return 0;
        }

        if (!device_supports_extensions(device, required_extensions)) {
            return 0;
        }

        // Note: only query swap chain support if the swap chain extension is present
        auto swap_chain_support = get_swap_chain_support(device);
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

    void choose_physical_device(const std::set<std::string>& required_extensions) {
        uint32_t count = 0;
        check_vulkan(vkEnumeratePhysicalDevices(vk_inst, &count, nullptr));
        if (count == 0) {
            throw std::runtime_error("Vulkan Error: no devices found");
        }

        std::vector<VkPhysicalDevice> devices(count);
        check_vulkan(vkEnumeratePhysicalDevices(vk_inst, &count, devices.data()));

        std::sort(devices.begin(), devices.end(),
                  [this, &required_extensions](const auto& lhs, const auto& rhs) {
                      return device_score(lhs, required_extensions)
                             < device_score(rhs, required_extensions);
                  });

        if (device_score(devices.back(), required_extensions) > 0) {
            vk_physical_device = devices.back();
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
            actual_extent.height
                = std::clamp(actual_extent.height, capabilities.minImageExtent.height,
                             capabilities.maxImageExtent.height);

            return actual_extent;
        }
    }

    void create_swap_chain(uint32_t w, uint32_t h) {
        auto swap_chain_details = get_swap_chain_support(vk_physical_device);

        auto format = choose_swap_surface_format(swap_chain_details.formats);
        auto present_mode = choose_swap_present_mode(swap_chain_details.present_modes);
        auto extent = choose_swap_extent(swap_chain_details.capabilities, w, h);

        uint32_t image_count = swap_chain_details.capabilities.minImageCount + 1;
        if (swap_chain_details.capabilities.maxImageCount > 0
            && image_count > swap_chain_details.capabilities.maxImageCount) {
            image_count = swap_chain_details.capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR chain_create_info{};
        chain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        chain_create_info.surface = vk_surface;

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
        VulkanQueueIndices indices = get_device_queues(vk_physical_device);
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

        chain_create_info.compositeAlpha
            = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;  // for blending windows

        chain_create_info.presentMode = present_mode;
        chain_create_info.clipped = VK_TRUE;

        chain_create_info.oldSwapchain = VK_NULL_HANDLE;

        check_vulkan(vkCreateSwapchainKHR(vk_logical_device, &chain_create_info, nullptr,
                                          &vk_swap_chain.handle));

        check_vulkan(vkGetSwapchainImagesKHR(vk_logical_device, vk_swap_chain.handle, &image_count,
                                             nullptr));
        vk_swap_chain.images.resize(image_count);
        check_vulkan(vkGetSwapchainImagesKHR(vk_logical_device, vk_swap_chain.handle, &image_count,
                                             vk_swap_chain.images.data()));

        vk_swap_chain.format = format.format;
        vk_swap_chain.extent = extent;

        vk_swap_chain.views.resize(vk_swap_chain.images.size());
        for (size_t i = 0; i < vk_swap_chain.images.size(); ++i) {
            VkImageViewCreateInfo view_create_info{};
            view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_create_info.image = vk_swap_chain.images[i];
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

            check_vulkan(vkCreateImageView(vk_logical_device, &view_create_info, nullptr,
                                           &vk_swap_chain.views[i]));
        }
    }

    void create_logical_device(const std::set<std::string>& required_extensions) {
        VulkanQueueIndices indices = get_device_queues(vk_physical_device);

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

        check_vulkan(vkCreateDevice(vk_physical_device, &create_info, nullptr, &vk_logical_device));

        vk_queues = VulkanQueues(indices, vk_logical_device);
    }

    template <size_t N> VkShaderModule create_shader_module(const std::array<uint32_t, N>& shader) {
        VkShaderModuleCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create_info.codeSize = shader.size() * sizeof(uint32_t);
        create_info.pCode = shader.data();

        VkShaderModule shader_module;
        check_vulkan(
            vkCreateShaderModule(vk_logical_device, &create_info, nullptr, &shader_module));

        return shader_module;
    }

    void create_render_pass() {
        VkAttachmentDescription color_attachment{};
        color_attachment.format = vk_swap_chain.format;
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

        check_vulkan(
            vkCreateRenderPass(vk_logical_device, &render_pass_info, nullptr, &vk_render_pass));
    }

    void create_graphics_pipeline() {
        auto vert_shader_module = create_shader_module(spor::shaders::test::vert);
        auto frag_shader_module = create_shader_module(spor::shaders::test::frag);

        std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
        {
            auto& shader_stage_info = shader_stages.emplace_back();
            shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;

            shader_stage_info.module = vert_shader_module;
            shader_stage_info.pName = "main";
        }

        {
            auto& shader_stage_info = shader_stages.emplace_back();
            shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;

            shader_stage_info.module = frag_shader_module;
            shader_stage_info.pName = "main";
        }

        VkPipelineVertexInputStateCreateInfo vertex_input{};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input.vertexBindingDescriptionCount = 0;
        vertex_input.pVertexBindingDescriptions = nullptr;  // Optional
        vertex_input.vertexAttributeDescriptionCount = 0;
        vertex_input.pVertexAttributeDescriptions = nullptr;  // Optional

        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly.primitiveRestartEnable = VK_FALSE;

        std::vector<VkDynamicState> dynamic_states
            = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

        VkPipelineDynamicStateCreateInfo dynamic_state_info{};
        dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
        dynamic_state_info.pDynamicStates = dynamic_states.data();

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(vk_swap_chain.extent.width);
        viewport.height = static_cast<float>(vk_swap_chain.extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = vk_swap_chain.extent;

        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f;  // Optional
        rasterizer.depthBiasClamp = 0.0f;           // Optional
        rasterizer.depthBiasSlopeFactor = 0.0f;     // Optional

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f;           // Optional
        multisampling.pSampleMask = nullptr;             // Optional
        multisampling.alphaToCoverageEnable = VK_FALSE;  // Optional
        multisampling.alphaToOneEnable = VK_FALSE;       // Optional

        VkPipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                                | VK_COLOR_COMPONENT_B_BIT
                                                | VK_COLOR_COMPONENT_A_BIT;
        color_blend_attachment.blendEnable = VK_TRUE;
        color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo color_blending{};
        color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blending.logicOpEnable = VK_FALSE;
        color_blending.logicOp = VK_LOGIC_OP_COPY;  // Optional
        color_blending.attachmentCount = 1;
        color_blending.pAttachments = &color_blend_attachment;
        color_blending.blendConstants[0] = 0.0f;  // Optional
        color_blending.blendConstants[1] = 0.0f;  // Optional
        color_blending.blendConstants[2] = 0.0f;  // Optional
        color_blending.blendConstants[3] = 0.0f;  // Optional

        VkPipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_info.setLayoutCount = 0;             // Optional
        pipeline_layout_info.pSetLayouts = nullptr;          // Optional
        pipeline_layout_info.pushConstantRangeCount = 0;     // Optional
        pipeline_layout_info.pPushConstantRanges = nullptr;  // Optional

        check_vulkan(vkCreatePipelineLayout(vk_logical_device, &pipeline_layout_info, nullptr,
                                            &vk_pipeline_layout));

        VkGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
        pipeline_info.pStages = shader_stages.data();

        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pDepthStencilState = nullptr;  // Optional
        pipeline_info.pColorBlendState = &color_blending;
        pipeline_info.pDynamicState = &dynamic_state_info;

        pipeline_info.layout = vk_pipeline_layout;

        pipeline_info.renderPass = vk_render_pass;
        pipeline_info.subpass = 0;

        pipeline_info.basePipelineHandle = VK_NULL_HANDLE;  // Optional
        pipeline_info.basePipelineIndex = -1;               // Optional

        check_vulkan(vkCreateGraphicsPipelines(vk_logical_device, VK_NULL_HANDLE, 1, &pipeline_info,
                                               nullptr, &vk_graphics_pipeline));

        vkDestroyShaderModule(vk_logical_device, vert_shader_module, nullptr);
        vkDestroyShaderModule(vk_logical_device, frag_shader_module, nullptr);
    }

    void create_framebuffers() {
        vk_swap_chain_framebuffers.resize(vk_swap_chain.views.size());

        for (size_t i = 0; i < vk_swap_chain.views.size(); i++) {
            VkImageView attachments[] = {vk_swap_chain.views[i]};

            VkFramebufferCreateInfo framebuffer_info{};
            framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_info.renderPass = vk_render_pass;
            framebuffer_info.attachmentCount = 1;
            framebuffer_info.pAttachments = attachments;
            framebuffer_info.width = vk_swap_chain.extent.width;
            framebuffer_info.height = vk_swap_chain.extent.height;
            framebuffer_info.layers = 1;

            check_vulkan(vkCreateFramebuffer(vk_logical_device, &framebuffer_info, nullptr,
                                             &vk_swap_chain_framebuffers[i]));
        }
    }

    void create_command_pool() {
        VulkanQueueIndices queue_indices = get_device_queues(vk_physical_device);

        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        // guaranteed to be present, but worth checking
        pool_info.queueFamilyIndex = queue_indices.graphics_family.value();

        check_vulkan(vkCreateCommandPool(vk_logical_device, &pool_info, nullptr, &vk_command_pool));
    }

    void create_command_buffer() {
        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = vk_command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;

        check_vulkan(vkAllocateCommandBuffers(vk_logical_device, &alloc_info, &vk_command_buffer));
    }

    void record_command_buffer(VkCommandBuffer command_buffer, size_t image_index) {
        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = 0;                   // Optional
        begin_info.pInheritanceInfo = nullptr;  // Optional

        check_vulkan(vkBeginCommandBuffer(command_buffer, &begin_info));

        VkRenderPassBeginInfo render_pass_info{};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_info.renderPass = vk_render_pass;
        render_pass_info.framebuffer = vk_swap_chain_framebuffers[image_index];

        render_pass_info.renderArea.offset = {0, 0};
        render_pass_info.renderArea.extent = vk_swap_chain.extent;

        VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        render_pass_info.clearValueCount = 1;
        render_pass_info.pClearValues = &clear_color;

        vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_graphics_pipeline);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(vk_swap_chain.extent.width);
        viewport.height = static_cast<float>(vk_swap_chain.extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(command_buffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = vk_swap_chain.extent;
        vkCmdSetScissor(command_buffer, 0, 1, &scissor);

        vkCmdDraw(command_buffer, 3, 1, 0, 0);

        vkCmdEndRenderPass(command_buffer);

        check_vulkan(vkEndCommandBuffer(command_buffer));
    }
};

VulkanBackend::VulkanBackend() : impl_(new ImplState{}) {}

VulkanBackend::~VulkanBackend() {
    impl_->sync_objects.destroy(impl_->vk_logical_device);

    vkDestroyCommandPool(impl_->vk_logical_device, impl_->vk_command_pool, nullptr);

    for (auto framebuffer : impl_->vk_swap_chain_framebuffers) {
        vkDestroyFramebuffer(impl_->vk_logical_device, framebuffer, nullptr);
    }

    vkDestroyPipeline(impl_->vk_logical_device, impl_->vk_graphics_pipeline, nullptr);
    vkDestroyRenderPass(impl_->vk_logical_device, impl_->vk_render_pass, nullptr);
    vkDestroyPipelineLayout(impl_->vk_logical_device, impl_->vk_pipeline_layout, nullptr);

    for (const auto& image_view : impl_->vk_swap_chain.views) {
        vkDestroyImageView(impl_->vk_logical_device, image_view, nullptr);
    }
    vkDestroySwapchainKHR(impl_->vk_logical_device, impl_->vk_swap_chain.handle, nullptr);

    vkDestroyDevice(impl_->vk_logical_device, nullptr);
    vkDestroySurfaceKHR(impl_->vk_inst, impl_->vk_surface, nullptr);

    vkDestroyInstance(impl_->vk_inst, nullptr);

    if (!impl_->window) {
        SDL_DestroyWindow(impl_->window);
    }

    SDL_Quit();
}

void VulkanBackend::create_main_window(std::string title, size_t w, size_t h) {
    check_sdl(SDL_Init(SDL_INIT_VIDEO));

    check_sdl(SDL_Vulkan_LoadLibrary(nullptr));

    impl_->create_vulkan_instance(title);

    SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN;

    impl_->window = SDL_CreateWindow(title.data(),         // window title
                                     static_cast<int>(w),  // window width in pixels
                                     static_cast<int>(h),  // window height in pixels
                                     window_flags);
    if (!impl_->window) {
        check_sdl(-1);
    }

    if (!SDL_Vulkan_CreateSurface(impl_->window, impl_->vk_inst, nullptr, &impl_->vk_surface)) {
        check_sdl(-1);
    }

    std::set<std::string> required_extensions = {
        std::string(VK_KHR_SWAPCHAIN_EXTENSION_NAME),
    };

    impl_->choose_physical_device(required_extensions);
    impl_->create_logical_device(required_extensions);

    int pixel_w, pixel_h;
    SDL_GetWindowSizeInPixels(impl_->window, &pixel_w, &pixel_h);
    impl_->create_swap_chain(static_cast<uint32_t>(pixel_w), static_cast<uint32_t>(pixel_h));

    impl_->create_render_pass();
    impl_->create_graphics_pipeline();

    impl_->create_framebuffers();

    impl_->create_command_pool();
    impl_->create_command_buffer();

    impl_->sync_objects.create(impl_->vk_logical_device);
}

void VulkanBackend::draw() {
    vkWaitForFences(impl_->vk_logical_device, 1, &impl_->sync_objects.in_flight, VK_TRUE, std::numeric_limits<uint64_t>::max());
    vkResetFences(impl_->vk_logical_device, 1, &impl_->sync_objects.in_flight);

    uint32_t image_index;
    vkAcquireNextImageKHR(impl_->vk_logical_device, impl_->vk_swap_chain.handle,
                          std::numeric_limits<uint64_t>::max(), impl_->sync_objects.image_available,
                          VK_NULL_HANDLE,
                          &image_index);

    vkResetCommandBuffer(impl_->vk_command_buffer, 0);
    impl_->record_command_buffer(impl_->vk_command_buffer, image_index);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore wait_semaphores[] = {impl_->sync_objects.image_available};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &impl_->vk_command_buffer;

    VkSemaphore signal_semaphores[] = {impl_->sync_objects.render_finished};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    check_vulkan(vkQueueSubmit(*impl_->vk_queues.graphics_queue, 1, &submit_info,
                               impl_->sync_objects.in_flight));

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;

    VkSwapchainKHR swap_chains[] = {impl_->vk_swap_chain.handle};
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swap_chains;
    present_info.pImageIndices = &image_index;

    present_info.pResults = nullptr;  // Optional

    vkQueuePresentKHR(*impl_->vk_queues.present_queue, &present_info);
}

bool VulkanBackend::poll_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        // close the window when user clicks the X button or alt-f4s
        if (event.type == SDL_EVENT_QUIT) {
            return false;
        }
    }

    return true;
}

void VulkanBackend::close() { vkDeviceWaitIdle(impl_->vk_logical_device); }

}  // namespace spor