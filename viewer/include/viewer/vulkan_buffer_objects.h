#pragma once

#include <variant>
#include <vector>

#include "viewer/vulkan_helpers.h"
#include "viewer/vulkan_base_objects.h"
#include "viewer/vulkan_render_objects.h"

namespace spor::vk {

class Buffer : public helpers::VulkanObject<Buffer> {
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

Buffer::ptr create_and_fill_transfer_buffer(SurfaceDevice::ptr surface_device,
                                            const unsigned char* data, size_t len);

CommandBuffer::ptr buffer_memcpy(SurfaceDevice::ptr device, CommandPool::ptr pool, Buffer::ptr src,
                                 Buffer::ptr dst, size_t size);

void submit_commands(CommandBuffer::ptr cmd_buffer, VkQueue queue, bool block = true);

class PersistentMapping : public helpers::NonCopyable {
public:
    PersistentMapping(Buffer::ptr buffer);
    ~PersistentMapping();

    PersistentMapping(PersistentMapping&&) noexcept;
    PersistentMapping& operator=(PersistentMapping&&) noexcept;

    Buffer::ptr buffer;
    void* mapped_mem{nullptr};
};

class Texture : public helpers::VulkanObject<Texture> {
public:
    ~Texture();

public:
    static ptr create(SurfaceDevice::ptr surface_device, size_t width, size_t height);

public:
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;

    size_t width, height;

private:
    SurfaceDevice::ptr surface_device_;

public:
    Texture(PrivateToken, SurfaceDevice::ptr surface_device, VkImage image, VkImageView view,
            VkDeviceMemory memory, size_t width, size_t height)
        : surface_device_(surface_device), image(image), view(view), memory(memory), width(width), height(height) {}
};

CommandBuffer::ptr transition_texture(SurfaceDevice::ptr device, CommandPool::ptr pool,
                                      Texture::ptr texture, VkImageLayout from_layout,
                                      VkImageLayout to_layout);

CommandBuffer::ptr texture_memcpy(SurfaceDevice::ptr device, CommandPool::ptr pool, Buffer::ptr src,
                                 Texture::ptr dst);

class Sampler : public helpers::VulkanObject<Sampler> {
public:
    ~Sampler();

public:
    static ptr create(SurfaceDevice::ptr surface_device, VkFilter filter = VK_FILTER_LINEAR,
                      VkSamplerAddressMode address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

public:
    VkSampler sampler;

private:
    SurfaceDevice::ptr surface_device_;

public:
    Sampler(PrivateToken, SurfaceDevice::ptr surface_device,
                   VkSampler sampler)
        : surface_device_(surface_device), sampler(sampler) {}
};

struct DescriptorInfo {
    VkDescriptorType type;
    VkShaderStageFlags shader_stages;

    struct DBuffer {
        Buffer::ptr buffer;
        size_t size = 0;
    };

    struct DSampler {
        Texture::ptr texture;
        Sampler::ptr sampler;
    };

    std::variant<DBuffer, DSampler> object;
};

class PipelineDescriptors : public helpers::VulkanObject<PipelineDescriptors> {
public:
    ~PipelineDescriptors();

public:
    static ptr create(SurfaceDevice::ptr device, const std::vector<DescriptorInfo>& descriptors);

public:
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor_set;

    VkDescriptorSetLayout layout;

private:
    SurfaceDevice::ptr device_;

public:
    PipelineDescriptors(PrivateToken, SurfaceDevice::ptr device, VkDescriptorPool descriptor_pool,
                        VkDescriptorSet descriptor_set, VkDescriptorSetLayout layout)
        : device_(device),
          descriptor_pool(descriptor_pool),
          descriptor_set(descriptor_set),
          layout(layout) {}
};

}  // namespace spor::vk