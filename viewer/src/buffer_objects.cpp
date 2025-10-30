#include "viewer\buffer_objects.h"

#include <map>
#include <stdexcept>

namespace spor::vk {

namespace helpers {
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
}  // namespace helpers

Buffer::~Buffer() {
    vkDestroyBuffer(surface_device_->device, buffer, nullptr);
    vkFreeMemory(surface_device_->device, memory, nullptr);
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
    helpers::check_vulkan(vkCreateBuffer(surface_device->device, &buffer_info, nullptr, &buffer));

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(surface_device->device, buffer, &mem_requirements);

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
    } else {
        props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    alloc_info.memoryTypeIndex = helpers::choose_memory_type(
        surface_device->physical_device, mem_requirements.memoryTypeBits, props);

    VkDeviceMemory memory;
    helpers::check_vulkan(vkAllocateMemory(surface_device->device, &alloc_info, nullptr, &memory));

    helpers::check_vulkan(vkBindBufferMemory(surface_device->device, buffer, memory, 0));

    return std::make_shared<Buffer>(PrivateToken{}, surface_device, buffer, memory, element_count,
                                    element_size);
}

void Buffer::set_memory(const unsigned char* data, size_t len) {
    if (len > size()) {
        throw std::invalid_argument("CPU memory size is greater than buffer size");
    }

    void* cpu_loc;
    helpers::check_vulkan(vkMapMemory(surface_device_->device, memory, 0, len, 0, &cpu_loc));
    std::memcpy(cpu_loc, data, len);
    vkUnmapMemory(surface_device_->device, memory);
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

CommandBuffer::ptr buffer_memcpy(SurfaceDevice::ptr device, CommandPool::ptr pool, Buffer::ptr src,
                                 Buffer::ptr dst, size_t size) {
    auto cmd_buffer = CommandBuffer::create(device, pool);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmd_buffer->command_buffer, &beginInfo);

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
    helpers::check_vulkan(vkMapMemory(buffer->surface_device_->device, buffer->memory, 0,
                                      buffer->size(), 0, &mapped_mem));
}

PersistentMapping::~PersistentMapping() {
    if (buffer && mapped_mem) {
        vkUnmapMemory(buffer->surface_device_->device, buffer->memory);
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
        descriptor_write.pImageInfo = nullptr;        // Optional
        descriptor_write.pTexelBufferView = nullptr;  // Optional

        switch (desc_info.type) {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: {
                auto& buffer_info = descriptor_buffer_infos.emplace_back();
                buffer_info.buffer = desc_info.object->buffer;
                buffer_info.offset = 0;
                buffer_info.range = desc_info.size;

                descriptor_write.pBufferInfo = &buffer_info;

                break;
            }
            case VK_DESCRIPTOR_TYPE_SAMPLER:
                break;
            default:
                break;  // remove the added write?
        }
    }

    vkUpdateDescriptorSets(device->device, static_cast<uint32_t>(set_writes.size()),
                           set_writes.data(), 0, nullptr);

    return std::make_shared<PipelineDescriptors>(PrivateToken{}, device, pool, descriptor_set,
                                                 descriptor_layout);
}

}  // namespace spor::vk