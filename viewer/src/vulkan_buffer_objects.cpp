#include "viewer\vulkan_buffer_objects.h"

#include <map>
#include <stdexcept>

namespace spor::vk {

Buffer::~Buffer() {
    vkDestroyBuffer(*surface_device_, buffer, nullptr);
    vkFreeMemory(*surface_device_, memory, nullptr);
}

Buffer::ptr Buffer::create(SurfaceDevice::ptr surface_device, VkBufferUsageFlags usage,
                           size_t element_count, size_t element_size) {
    size_t size = element_count * element_size;

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer;
    helpers::check_vulkan(vkCreateBuffer(*surface_device, &buffer_info, nullptr, &buffer));

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(*surface_device, buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;

    VkMemoryPropertyFlags props;
    if ((usage & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) || (usage & VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    } else if (usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) {
        props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    } else if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
        props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    } else if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
        props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    } else {
        props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    alloc_info.memoryTypeIndex = helpers::choose_memory_type(
        surface_device->physical_device, mem_requirements.memoryTypeBits, props);

    VkDeviceMemory memory;
    helpers::check_vulkan(vkAllocateMemory(*surface_device, &alloc_info, nullptr, &memory));

    helpers::check_vulkan(vkBindBufferMemory(*surface_device, buffer, memory, 0));

    return std::make_shared<Buffer>(PrivateToken{}, surface_device, buffer, memory, element_count,
                                    element_size);
}

void Buffer::set_memory(const unsigned char* data, size_t len) {
    if (len > size()) {
        throw std::invalid_argument("CPU memory size is greater than buffer size");
    }

    void* cpu_loc;
    helpers::check_vulkan(vkMapMemory(*surface_device_, memory, 0, len, 0, &cpu_loc));
    std::memcpy(cpu_loc, data, len);
    vkUnmapMemory(*surface_device_, memory);
}

size_t Buffer::size() const { return element_count * element_size; }

Buffer::ptr create_vertex_buffer(SurfaceDevice::ptr surface_device, size_t element_count,
                                 size_t element_size) {
    return Buffer::create(surface_device,
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          element_count, element_size);
}

Buffer::ptr create_index_buffer(SurfaceDevice::ptr surface_device, size_t element_count,
                                size_t element_size) {
    return Buffer::create(surface_device,
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                          element_count, element_size);
}

Buffer::ptr create_uniform_buffer(SurfaceDevice::ptr surface_device, size_t element_count,
                                  size_t element_size) {
    return Buffer::create(surface_device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, element_count,
                          element_size);
}

Buffer::ptr create_storage_buffer(SurfaceDevice::ptr surface_device, VkBufferUsageFlags aliasing,
                                  size_t element_count, size_t element_size) {
    return Buffer::create(
        surface_device,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | aliasing,
        element_count, element_size);
}

Buffer::ptr create_and_fill_transfer_buffer(SurfaceDevice::ptr surface_device,
                                            const unsigned char* data, size_t len) {
    auto buffer = Buffer::create(surface_device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, len, 1);
    buffer->set_memory(data, len);

    return buffer;
}

CommandBuffer::ptr buffer_memcpy(SurfaceDevice::ptr device, CommandPool::ptr pool, Buffer::ptr src,
                                 Buffer::ptr dst, size_t size) {
    auto cmd_buffer = CommandBuffer::create(device, pool);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd_buffer->command_buffer, &begin_info);

    VkBufferCopy copy_region{};
    copy_region.srcOffset = 0;  // Optional
    copy_region.dstOffset = 0;  // Optional
    copy_region.size = size;
    vkCmdCopyBuffer(cmd_buffer->command_buffer, src->buffer, dst->buffer, 1, &copy_region);

    vkEndCommandBuffer(cmd_buffer->command_buffer);

    return cmd_buffer;
}

void submit_commands(CommandBuffer::ptr cmd_buffer, VkQueue queue, bool block) {
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd_buffer->command_buffer;

    vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);

    if (block) {
        vkQueueWaitIdle(queue);
    }
}

PersistentMapping::PersistentMapping(Buffer::ptr buffer) : buffer(buffer) {
    helpers::check_vulkan(
        vkMapMemory(*buffer->surface_device_, buffer->memory, 0, buffer->size(), 0, &mapped_mem));
}

PersistentMapping::~PersistentMapping() {
    if (buffer && mapped_mem) {
        vkUnmapMemory(*buffer->surface_device_, buffer->memory);
    }
}

PersistentMapping::PersistentMapping(PersistentMapping&& other) noexcept {
    *this = std::move(other);
}

PersistentMapping& PersistentMapping::operator=(PersistentMapping&& other) noexcept {
    std::swap(buffer, other.buffer);
    std::swap(mapped_mem, other.mapped_mem);

    return *this;
}

Texture::~Texture() {
    vkDestroyImageView(*surface_device_, view, nullptr);
    vkDestroyImage(*surface_device_, image, nullptr);
    vkFreeMemory(*surface_device_, memory, nullptr);
}

Texture::ptr Texture::create(SurfaceDevice::ptr surface_device, size_t width, size_t height) {
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;  // supported on basically all modern hardware

    auto [image, memory] = helpers::create_image(
        surface_device->device, surface_device->physical_device, width, height, format,
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    auto view
        = helpers::create_image_view(*surface_device, image, format, VK_IMAGE_ASPECT_COLOR_BIT);

    return std::make_shared<Texture>(PrivateToken{}, surface_device, image, view, memory, width,
                                     height);
}

CommandBuffer::ptr transition_texture(SurfaceDevice::ptr device, CommandPool::ptr pool,
                                      Texture::ptr texture, VkImageLayout from_layout,
                                      VkImageLayout to_layout) {
    auto cmd_buffer = CommandBuffer::create(device, pool);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd_buffer->command_buffer, &begin_info);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = from_layout;
    barrier.newLayout = to_layout;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.image = texture->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage;
    VkPipelineStageFlags dst_stage;

    if (from_layout == VK_IMAGE_LAYOUT_UNDEFINED
        && to_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (from_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
               && to_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        throw std::invalid_argument("Unsupported layout transition");
    }

    vkCmdPipelineBarrier(cmd_buffer->command_buffer, src_stage, dst_stage, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd_buffer->command_buffer);

    return cmd_buffer;
}

CommandBuffer::ptr texture_memcpy(SurfaceDevice::ptr device, CommandPool::ptr pool, Buffer::ptr src,
                                  Texture::ptr dst) {
    auto cmd_buffer = CommandBuffer::create(device, pool);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd_buffer->command_buffer, &begin_info);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(dst->width), static_cast<uint32_t>(dst->height), 1};

    vkCmdCopyBufferToImage(cmd_buffer->command_buffer, src->buffer, dst->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    vkEndCommandBuffer(cmd_buffer->command_buffer);

    return cmd_buffer;
}

Sampler::~Sampler() { vkDestroySampler(*surface_device_, sampler, nullptr); }

Sampler::ptr Sampler::create(SurfaceDevice::ptr surface_device, VkFilter filter,
                             VkSamplerAddressMode address_mode) {
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = filter;
    sampler_info.minFilter = filter;

    sampler_info.addressModeU = address_mode;
    sampler_info.addressModeV = address_mode;
    sampler_info.addressModeW = address_mode;

    sampler_info.anisotropyEnable = VK_TRUE;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(surface_device->physical_device, &properties);
    sampler_info.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;

    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;

    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias = 0.0f;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 0.0f;

    VkSampler sampler;
    helpers::check_vulkan(vkCreateSampler(*surface_device, &sampler_info, nullptr, &sampler));

    return std::make_shared<Sampler>(PrivateToken{}, surface_device, sampler);
}

PipelineDescriptors::~PipelineDescriptors() {
    vkDestroyDescriptorSetLayout(device_->device, layout, nullptr);
    vkDestroyDescriptorPool(device_->device, descriptor_pool, nullptr);
}

PipelineDescriptors::ptr PipelineDescriptors::create(
    SurfaceDevice::ptr device, const std::vector<DescriptorInfo>& descriptors) {
    std::map<VkDescriptorType, size_t> type_indices;
    std::vector<VkDescriptorPoolSize> pool_sizes;

    std::vector<VkDescriptorSetLayoutBinding> set_layouts;

    for (const auto& desc_info : descriptors) {
        if (type_indices.count(desc_info.type)) {
            ++pool_sizes[type_indices[desc_info.type]].descriptorCount;
        } else {
            pool_sizes.push_back({desc_info.type, 1});
            type_indices[desc_info.type] = pool_sizes.size() - 1;
        }

        auto& layout = set_layouts.emplace_back();
        layout.binding = static_cast<uint32_t>(set_layouts.size() - 1);
        layout.descriptorType = desc_info.type;
        layout.descriptorCount = 1;
        layout.stageFlags = desc_info.shader_stages;
        layout.pImmutableSamplers = nullptr;  // Optional
    }

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    pool_info.pPoolSizes = pool_sizes.data();
    pool_info.maxSets = static_cast<uint32_t>(descriptors.size());

    VkDescriptorPool pool;
    helpers::check_vulkan(vkCreateDescriptorPool(device->device, &pool_info, nullptr, &pool));

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(set_layouts.size());
    layout_info.pBindings = set_layouts.data();

    VkDescriptorSetLayout descriptor_layout;
    vk::helpers::check_vulkan(
        vkCreateDescriptorSetLayout(device->device, &layout_info, nullptr, &descriptor_layout));

    std::vector<VkDescriptorSetLayout> layouts(1, descriptor_layout);
    VkDescriptorSetAllocateInfo set_alloc_info{};
    set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set_alloc_info.descriptorPool = pool;
    set_alloc_info.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    set_alloc_info.pSetLayouts = layouts.data();

    VkDescriptorSet descriptor_set;
    helpers::check_vulkan(
        vkAllocateDescriptorSets(device->device, &set_alloc_info, &descriptor_set));

    std::vector<VkWriteDescriptorSet> set_writes;

    // we will store pointers to these elements, but by reserving the maximum size for each, we
    // *won't* need to worry about reallocs moving them
    std::vector<VkDescriptorBufferInfo> descriptor_buffer_infos;
    descriptor_buffer_infos.reserve(descriptors.size());

    std::vector<VkDescriptorImageInfo> descriptor_image_infos;
    descriptor_image_infos.reserve(descriptors.size());

    for (const auto& desc_info : descriptors) {
        auto& descriptor_write = set_writes.emplace_back();
        descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptor_write.dstSet = descriptor_set;
        descriptor_write.dstBinding = static_cast<uint32_t>(
            set_writes.size() - 1);  // this is counting up on vector size, but is meant to be
                                     // synced with layout.binding above
        descriptor_write.dstArrayElement = 0;
        descriptor_write.descriptorCount = 1;
        descriptor_write.descriptorType = desc_info.type;

        descriptor_write.pBufferInfo = nullptr;
        descriptor_write.pImageInfo = nullptr;
        descriptor_write.pTexelBufferView = nullptr;

        if (auto* buffer = std::get_if<DescriptorInfo::DBuffer>(&desc_info.object)) {
            auto& buffer_info = descriptor_buffer_infos.emplace_back();
            buffer_info.buffer = buffer->buffer->buffer;  // :)
            buffer_info.offset = 0;
            buffer_info.range = buffer->size;

            descriptor_write.pBufferInfo = &buffer_info;
        } else if (auto* texture = std::get_if<DescriptorInfo::DSampler>(&desc_info.object)) {
            auto& image_info = descriptor_image_infos.emplace_back();
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            image_info.imageView = texture->texture->view;
            image_info.sampler = texture->sampler->sampler;  // :)

            descriptor_write.pImageInfo = &image_info;
        } else {
            // remove the added write?
        }
    }

    vkUpdateDescriptorSets(device->device, static_cast<uint32_t>(set_writes.size()),
                           set_writes.data(), 0, nullptr);

    return std::make_shared<PipelineDescriptors>(PrivateToken{}, device, pool, descriptor_set,
                                                 descriptor_layout);
}

}  // namespace spor::vk