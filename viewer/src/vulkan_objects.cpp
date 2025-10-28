#include "viewer/vulkan_objects.h"

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

void check_vulkan(VkResult result) {
    if (result != VK_SUCCESS) {
        std::cerr << fmt::format("Vulkan Error: {}", string_VkResult(result)) << std::endl;
        throw std::runtime_error(fmt::format("Vulkan Error: %s", string_VkResult(result)));
    }
}

struct VulkanSwapChainDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> present_modes;
};

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

VkRenderPass create_render_pass(SurfaceDevice::ptr surface_device, SwapChain::ptr swap_chain) {
    VkAttachmentDescription color_attachment{};
    color_attachment.format = swap_chain->format;
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
        vkCreateRenderPass(surface_device->device, &render_pass_info, nullptr, &render_pass));

    return render_pass;
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

WindowHandle::WindowHandle(WindowHandle&& other) : window_(other.window_) {
    other.window_ = nullptr;
}

WindowHandle& WindowHandle::operator=(WindowHandle&& other) {
    std::swap(window_, other.window_);
    if (&other != this) {
        other.window_ = nullptr;
    }

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

    VulkanQueueIndices indices = helpers::get_device_queues(physical_device, surface);

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

    VulkanQueues queues(indices, logical_device);

    return std::make_shared<SurfaceDevice>(PrivateToken{}, inst, window, physical_device, surface,
                                           logical_device, indices, queues);
}

SwapChain::~SwapChain() {
    for (const auto& image_view : swap_chain_views) {
        vkDestroyImageView(surface_device_->device, image_view, nullptr);
    }
    vkDestroySwapchainKHR(surface_device_->device, swap_chain, nullptr);
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
        vkCreateSwapchainKHR(surface_device->device, &chain_create_info, nullptr, &swap_chain));

    helpers::check_vulkan(
        vkGetSwapchainImagesKHR(surface_device->device, swap_chain, &image_count, nullptr));

    std::vector<VkImage> swap_chain_images(image_count);
    helpers::check_vulkan(vkGetSwapchainImagesKHR(surface_device->device, swap_chain, &image_count,
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

        helpers::check_vulkan(vkCreateImageView(surface_device->device, &view_create_info, nullptr,
                                                &swap_chain_views[i]));
    }

    return std::make_shared<SwapChain>(PrivateToken{}, surface_device, swap_chain,
                                       swap_chain_images, swap_chain_views, format.format, extent);
}


DescriptorSetLayout::~DescriptorSetLayout() {
    vkDestroyDescriptorSetLayout(surface_device_->device, descriptor_layout, nullptr);
}

DescriptorSetLayout::ptr DescriptorSetLayout::create(SurfaceDevice::ptr surface_device,
                                                     uint32_t binding) {
    VkDescriptorSetLayoutBinding layout{};
    layout.binding = binding;
    layout.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    layout.descriptorCount = 1;
    layout.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layout.pImmutableSamplers = nullptr;  // Optional

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &layout;

    VkDescriptorSetLayout descriptor_layout;
    vk::helpers::check_vulkan(vkCreateDescriptorSetLayout(surface_device->device, &layout_info,
                                                          nullptr, &descriptor_layout));

    return std::make_shared<DescriptorSetLayout>(PrivateToken{}, surface_device, descriptor_layout);
}

GraphicsPipeline::~GraphicsPipeline() {}

void GraphicsPipelineBuilder::add_shader(VkShaderStageFlagBits stage, const uint32_t* shader_data,
                                         size_t len) {
    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = len * sizeof(uint32_t);
    create_info.pCode = shader_data;

    VkShaderModule shader_module;
    helpers::check_vulkan(
        vkCreateShaderModule(surface_device_->device, &create_info, nullptr, &shader_module));
    shaders_.push_back(shader_module);

    auto& shader_stage_info = shader_stages_.emplace_back();
    shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stage_info.stage = stage;

    shader_stage_info.module = shader_module;
    shader_stage_info.pName = "main";
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::set_vertex_descriptors(
    VkVertexInputBindingDescription binding_desc,
    std::vector<VkVertexInputAttributeDescription> attrib_descs) {
    vertex_descriptors_ = VertexDescriptors{binding_desc, std::move(attrib_descs)};

    return *this;
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::add_descriptor_set(
    DescriptorSetLayout::ptr descriptor_set) {
    descriptor_sets_.push_back(descriptor_set->descriptor_layout);
    return *this;
}

GraphicsPipeline::ptr GraphicsPipelineBuilder::build() {
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 0;
    vertex_input.pVertexBindingDescriptions = nullptr;
    vertex_input.vertexAttributeDescriptionCount = 0;
    vertex_input.pVertexAttributeDescriptions = nullptr;

    if (vertex_descriptors_) {
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &vertex_descriptors_->binding_desc;

        vertex_input.vertexAttributeDescriptionCount
            = static_cast<uint32_t>(vertex_descriptors_->attrib_descs.size());
        vertex_input.pVertexAttributeDescriptions = vertex_descriptors_->attrib_descs.data();
    }

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
    viewport.width = static_cast<float>(swap_chain_->extent.width);
    viewport.height = static_cast<float>(swap_chain_->extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swap_chain_->extent;

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
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

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
                                            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
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

    pipeline_layout_info.setLayoutCount = static_cast<uint32_t>(descriptor_sets_.size());
    pipeline_layout_info.pSetLayouts = descriptor_sets_.data();

    pipeline_layout_info.pushConstantRangeCount = 0;     // Optional
    pipeline_layout_info.pPushConstantRanges = nullptr;  // Optional

    VkPipelineLayout layout;
    helpers::check_vulkan(
        vkCreatePipelineLayout(surface_device_->device, &pipeline_layout_info, nullptr, &layout));

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = static_cast<uint32_t>(shader_stages_.size());
    pipeline_info.pStages = shader_stages_.data();

    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = nullptr;  // Optional
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state_info;

    pipeline_info.layout = layout;

    auto render_pass = helpers::create_render_pass(surface_device_, swap_chain_);
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;

    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;  // Optional
    pipeline_info.basePipelineIndex = -1;               // Optional

    VkPipeline pipeline;
    helpers::check_vulkan(vkCreateGraphicsPipelines(surface_device_->device, VK_NULL_HANDLE, 1,
                                                    &pipeline_info, nullptr, &pipeline));

    for (auto& shader_module : shaders_) {
        vkDestroyShaderModule(surface_device_->device, shader_module, nullptr);
    }

    for (auto& descriptor_set : descriptor_sets_) {
        vkDestroyDescriptorSetLayout(surface_device_->device, descriptor_set, nullptr);
    }

    shaders_.clear();
    shader_stages_.clear();
    descriptor_sets_.clear();

    return std::make_shared<GraphicsPipeline>(GraphicsPipeline::PrivateToken{}, surface_device_,
                                              swap_chain_, render_pass, layout, pipeline);
}

CommandPool::~CommandPool() {
    vkDestroyCommandPool(surface_device_->device, command_pool, nullptr);
}

CommandPool::ptr CommandPool::create(SurfaceDevice::ptr surface_device) {
    VulkanQueueIndices queue_indices = surface_device->indices;

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    // guaranteed to be present, but worth checking
    pool_info.queueFamilyIndex = queue_indices.graphics_family.value();

    VkCommandPool pool;
    helpers::check_vulkan(vkCreateCommandPool(surface_device->device, &pool_info, nullptr, &pool));

    return std::make_shared<CommandPool>(PrivateToken{}, surface_device, pool);
}

CommandBuffer::ptr CommandBuffer::create(SurfaceDevice::ptr surface_device,
                                         CommandPool::ptr command_pool) {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_pool->command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer buffer;
    helpers::check_vulkan(vkAllocateCommandBuffers(surface_device->device, &alloc_info, &buffer));

    return std::make_shared<CommandBuffer>(PrivateToken{}, surface_device, buffer);
}

SwapChainFramebuffers::~SwapChainFramebuffers() {} // TODO: destroy framebuffers

SwapChainFramebuffers::ptr SwapChainFramebuffers::create(SurfaceDevice::ptr surface_device,
                                                         SwapChain::ptr swap_chain,
                                                         GraphicsPipeline::ptr pipeline) {
    std::vector<VkFramebuffer> framebuffers(swap_chain->swap_chain_views.size());

    for (size_t i = 0; i < swap_chain->swap_chain_views.size(); i++) {
        VkImageView attachments[] = {swap_chain->swap_chain_views[i]};

        VkFramebufferCreateInfo framebuffer_info{};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = pipeline->render_pass;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = swap_chain->extent.width;
        framebuffer_info.height = swap_chain->extent.height;
        framebuffer_info.layers = 1;

        helpers::check_vulkan(vkCreateFramebuffer(surface_device->device, &framebuffer_info,
                                                  nullptr, &framebuffers[i]));

    }

    return std::make_shared<SwapChainFramebuffers>(PrivateToken{}, surface_device, swap_chain,
                                                   pipeline, std::move(framebuffers));
}

record_commands::record_commands(CommandBuffer::ptr command_buffer)
    : command_buffer_(command_buffer) {
    helpers::check_vulkan(vkResetCommandBuffer(command_buffer->command_buffer, 0));

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

record_commands::record_commands(record_commands&& other) : record_commands(other.command_buffer_) {
    other.command_buffer_ = nullptr;
}

record_commands& record_commands::operator=(record_commands&& other) {
    std::swap(command_buffer_, other.command_buffer_);
    if (&other != this) {
        other.command_buffer_ = nullptr;
    }

    return *this;
}

render_pass::render_pass(CommandBuffer::ptr command_buffer, GraphicsPipeline::ptr pipeline,
                         VkFramebuffer framebuffer, VkRect2D area)
    : command_buffer_(command_buffer) {
    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = pipeline->render_pass;
    render_pass_info.framebuffer = framebuffer;

    render_pass_info.renderArea = area;

    VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass(command_buffer_->command_buffer, &render_pass_info,
                         VK_SUBPASS_CONTENTS_INLINE);
}

render_pass::~render_pass() {
    if (command_buffer_) {
        vkCmdEndRenderPass(command_buffer_->command_buffer);
    }
}

render_pass::render_pass(render_pass&& other) : command_buffer_(other.command_buffer_) {
    other.command_buffer_ = nullptr;
}

render_pass& render_pass::operator=(render_pass&& other) {
    std::swap(command_buffer_, other.command_buffer_);
    if (&other != this) {
        other.command_buffer_ = nullptr;
    }

    return *this;
}

DefaultRenderSyncObjects::ptr DefaultRenderSyncObjects::create(vk::SurfaceDevice::ptr device) {
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphore image_available;
    VkSemaphore render_finished;
    VkFence in_flight;
    helpers::check_vulkan(
        vkCreateSemaphore(device->device, &semaphore_info, nullptr, &image_available));
    helpers::check_vulkan(
        vkCreateSemaphore(device->device, &semaphore_info, nullptr, &render_finished));
    helpers::check_vulkan(vkCreateFence(device->device, &fence_info, nullptr, &in_flight));

    return std::make_shared<DefaultRenderSyncObjects>(PrivateToken{}, device, image_available,
                                                      render_finished, in_flight);
}

DefaultRenderSyncObjects::~DefaultRenderSyncObjects() {
    vkDestroyFence(surface_device_->device, in_flight, nullptr);
    vkDestroySemaphore(surface_device_->device, render_finished, nullptr);
    vkDestroySemaphore(surface_device_->device, image_available, nullptr);
}

}  // namespace spor::vk