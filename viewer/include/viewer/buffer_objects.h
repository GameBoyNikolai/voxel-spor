#pragma once

#include "viewer/vulkan_objects.h"

namespace spor::vk {

class Buffer : public VulkanObject<Buffer> {
public:
    ~Buffer();

public:
    static ptr create(SurfaceDevice::ptr surface_device, VkBufferUsageFlags usage,
                      size_t element_count, size_t element_size);

    void set_memory(const unsigned char* data, size_t len);

    template <typename T> void set_memory(const std::vector<T>& data) {
        set_memory(reinterpret_cast<const unsigned char*>(data.data()), data.size() * sizeof(T));
    }

    size_t size() const;

public:
    VkBuffer buffer;
    VkDeviceMemory memory;
    size_t element_count;
    size_t element_size;

private:
    friend class PersistentMapping;  // to access device easily

    SurfaceDevice::ptr surface_device_;

public:
    Buffer(PrivateToken, SurfaceDevice::ptr surface_device, VkBuffer buffer, VkDeviceMemory memory,
           size_t element_count, size_t element_size)
        : surface_device_(surface_device),
          buffer(buffer),
          memory(memory),
          element_count(element_count),
          element_size(element_size) {}
};

Buffer::ptr create_vertex_buffer(SurfaceDevice::ptr surface_device, size_t element_count,
                                 size_t element_size);

Buffer::ptr create_index_buffer(SurfaceDevice::ptr surface_device, size_t element_count,
                                size_t element_size);

Buffer::ptr create_uniform_buffer(SurfaceDevice::ptr surface_device, size_t element_count,
                                  size_t element_size);

template <typename T> Buffer::ptr create_and_fill_transfer_buffer(SurfaceDevice::ptr surface_device,
                                                                  const std::vector<T>& data) {
    auto buffer
        = Buffer::create(surface_device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, data.size(), sizeof(T));
    buffer->set_memory(data);

    return buffer;
}

CommandBuffer::ptr buffer_memcpy(SurfaceDevice::ptr device, CommandPool::ptr pool, Buffer::ptr src,
                                 Buffer::ptr dst, size_t size);

void submit_commands(CommandBuffer::ptr cmd_buffer, VkQueue queue, bool block = true);

// TODO: go and fix all move ctors/ops to include noexcept and proper destruction/exchange of
// resources
class PersistentMapping {
public:
    PersistentMapping(Buffer::ptr buffer);
    ~PersistentMapping();

    PersistentMapping(PersistentMapping&&);
    PersistentMapping& operator=(PersistentMapping&&);

    PersistentMapping(const PersistentMapping&) = delete;
    PersistentMapping& operator=(const PersistentMapping&) = delete;

    Buffer::ptr buffer;
    void* mapped_mem{nullptr};
};

class DescriptorPool : public VulkanObject<DescriptorPool> {
public:
    ~DescriptorPool();

public:
    static ptr create(SurfaceDevice::ptr surface_device, size_t desc_count);

public:
    VkDescriptorPool pool;
    size_t desc_count;

private:
    SurfaceDevice::ptr surface_device_;

public:
    DescriptorPool(PrivateToken, SurfaceDevice::ptr surface_device, VkDescriptorPool pool,
                   size_t desc_count)
        : surface_device_(surface_device), pool(pool), desc_count(desc_count) {}
};

class DescriptorSet : public VulkanObject<DescriptorSet> {
public:
    static ptr create(SurfaceDevice::ptr surface_device, DescriptorPool::ptr pool,
                      DescriptorSetLayout::ptr set_layout);

public:
    VkDescriptorSet descriptor_set;

private:
    SurfaceDevice::ptr surface_device_;
    DescriptorPool::ptr pool_;
    DescriptorSetLayout::ptr layout_;

public:
    DescriptorSet(PrivateToken, SurfaceDevice::ptr surface_device, DescriptorPool::ptr pool,
                  DescriptorSetLayout::ptr set_layout,
                  VkDescriptorSet descriptor_set)
        : surface_device_(surface_device),
          pool_(pool),
          layout_(set_layout),
          descriptor_set(descriptor_set) {}
};

void update_descriptor_sets(SurfaceDevice::ptr device, Buffer::ptr ubo, size_t element_size,
                            DescriptorSet::ptr descriptor_set);

}  // namespace spor::vk