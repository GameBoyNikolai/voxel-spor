#include "viewer\buffer_objects.h"

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

PersistentMapping::PersistentMapping(PersistentMapping&& other)
    : buffer(other.buffer), mapped_mem(other.mapped_mem) {
    other.buffer = nullptr;
    other.mapped_mem = nullptr;
}

PersistentMapping& PersistentMapping::operator=(PersistentMapping&& other) {
    std::swap(buffer, other.buffer);
    std::swap(mapped_mem, other.mapped_mem);

    if (this != &other) {
        other.buffer = nullptr;
        other.mapped_mem = nullptr;
    }

    return *this;
}

DescriptorPool::~DescriptorPool() {
    vkDestroyDescriptorPool(surface_device_->device, pool, nullptr);
}

DescriptorPool::ptr DescriptorPool::create(SurfaceDevice::ptr surface_device, size_t desc_count) {
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_size.descriptorCount = static_cast<uint32_t>(desc_count);

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = 1;

    VkDescriptorPool pool;
    helpers::check_vulkan(
        vkCreateDescriptorPool(surface_device->device, &pool_info, nullptr, &pool));

    return std::make_shared<DescriptorPool>(PrivateToken{}, surface_device, pool, desc_count);
}

DescriptorSet::ptr DescriptorSet::create(SurfaceDevice::ptr surface_device,
                                         DescriptorPool::ptr pool,
                                         DescriptorSetLayout::ptr set_layout) {
    std::vector<VkDescriptorSetLayout> layouts(1, set_layout->descriptor_layout);
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = pool->pool;
    alloc_info.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    alloc_info.pSetLayouts = layouts.data();

    VkDescriptorSet descriptor_set;
    helpers::check_vulkan(
        vkAllocateDescriptorSets(surface_device->device, &alloc_info, &descriptor_set));

    return std::make_shared<DescriptorSet>(PrivateToken{}, surface_device, pool, set_layout, descriptor_set);
}

void update_descriptor_sets(SurfaceDevice::ptr device, Buffer::ptr ubo, size_t element_size,
                            DescriptorSet::ptr descriptor_set) {
    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = ubo->buffer;
    buffer_info.offset = 0;
    buffer_info.range = element_size;

    VkWriteDescriptorSet descriptor_write{};
    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.dstSet = descriptor_set->descriptor_set;
    descriptor_write.dstBinding = 0;
    descriptor_write.dstArrayElement = 0;

    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_write.descriptorCount = 1;

    descriptor_write.pBufferInfo = &buffer_info;
    descriptor_write.pImageInfo = nullptr;        // Optional
    descriptor_write.pTexelBufferView = nullptr;  // Optional

    vkUpdateDescriptorSets(device->device, 1, &descriptor_write, 0, nullptr);
}

}  // namespace spor::vk