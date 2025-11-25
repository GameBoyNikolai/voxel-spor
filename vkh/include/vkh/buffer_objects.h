#pragma once

#include <variant>
#include <vector>

#include "vkh/base_objects.h"
#include "vkh/helpers.h"

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
    template <typename T> friend class PersistentMapping;  // to access device easily

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

Buffer::ptr create_storage_buffer(SurfaceDevice::ptr surface_device, VkBufferUsageFlags aliasing,
                                  size_t element_count, size_t element_size);

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

template <typename T> class PersistentMapping : public helpers::NonCopyable {
public:
    PersistentMapping(Buffer::ptr buffer) : buffer(buffer) {
        helpers::check_vulkan(vkMapMemory(*buffer->surface_device_, buffer->memory, 0,
                                          buffer->size(), 0,
                                          reinterpret_cast<void**>(&mapped_mem)));
    }

    ~PersistentMapping() {
        if (buffer && mapped_mem) {
            vkUnmapMemory(*buffer->surface_device_, buffer->memory);
        }
    }

    PersistentMapping(PersistentMapping&& other) noexcept { *this = std::move(other); }

    PersistentMapping& operator=(PersistentMapping&& other) noexcept {
        std::swap(buffer, other.buffer);
        std::swap(mapped_mem, other.mapped_mem);

        return *this;
    }

public:
    T& operator[](size_t i) {
        if (i >= buffer->element_count) {
            throw std::out_of_range("Mapped memory index out of bounds");
        }

        return mapped_mem[i];
    }

public:
    Buffer::ptr buffer;
    T* mapped_mem{nullptr};
};

class Texture : public helpers::VulkanObject<Texture> {
public:
    ~Texture();

public:
    static ptr create(SurfaceDevice::ptr surface_device, size_t width, size_t height);

    helpers::ImageView image_view() const { return helpers::ImageView{image, view, width, height}; }

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
        : surface_device_(surface_device),
          image(image),
          view(view),
          memory(memory),
          width(width),
          height(height) {}
};

class DrawImage : public helpers::VulkanObject<DrawImage> {
public:
    ~DrawImage();

public:
    static ptr create(SurfaceDevice::ptr surface_device, size_t width, size_t height);

    helpers::ImageView image_view() const { return helpers::ImageView{image, view, width, height}; }

public:
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;

    size_t width, height;

private:
    SurfaceDevice::ptr surface_device_;

public:
    DrawImage(PrivateToken, SurfaceDevice::ptr surface_device, VkImage image, VkImageView view,
            VkDeviceMemory memory, size_t width, size_t height)
        : surface_device_(surface_device),
          image(image),
          view(view),
          memory(memory),
          width(width),
          height(height) {}
};

CommandBuffer::ptr transition_texture(SurfaceDevice::ptr device, CommandPool::ptr pool,
                                      Texture::ptr texture, VkImageLayout from_layout,
                                      VkImageLayout to_layout);

CommandBuffer::ptr texture_memcpy(SurfaceDevice::ptr device, CommandPool::ptr pool, Buffer::ptr src,
                                  Texture::ptr dst);

class Sampler : public helpers::VulkanObject<Sampler> {
public:
    ~Sampler();

    operator VkSampler() const { return sampler; }

public: 
    static ptr create(SurfaceDevice::ptr surface_device, VkFilter filter = VK_FILTER_LINEAR,
                      VkSamplerAddressMode address_mode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

public:
    VkSampler sampler;

private:
    SurfaceDevice::ptr surface_device_;

public:
    Sampler(PrivateToken, SurfaceDevice::ptr surface_device, VkSampler sampler)
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

// TODO: along with the GraphicsPipeline change, separate this object into parameters of a GP and
// arguments
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