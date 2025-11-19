#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "viewer/glm_decl.h"
#include "viewer/vulkan_base_objects.h"
#include "viewer/vulkan_buffer_objects.h"
#include "viewer/vulkan_helpers.h"
#include "vulkan/vulkan.h"

namespace spor::vk {

class Kernel : public helpers::VulkanObject<Kernel> {
public:
    ~Kernel();

public:
    friend class Invoker;

    operator VkPipeline() const { return compute_pipeline; };

public:
    enum class ParamType {
        kUBO,
        kSSBO,
        kStorageImage,
    };

    // this will construct the descriptor pool, layout and set for the given list of parameters
    static ptr create(SurfaceDevice::ptr surface_device,
                      const std::vector<uint32_t>& compiled_shader,
                      const std::vector<ParamType> param_types);

public:
    VkPipelineLayout pipeline_layout;
    VkPipeline compute_pipeline;

private:
    SurfaceDevice::ptr device_;

    VkDescriptorPool descriptor_pool_;
    VkDescriptorSetLayout descriptor_layout_;
    VkDescriptorSet descriptor_set_;

    std::vector<ParamType> parameters_;

public:
    Kernel(PrivateToken, SurfaceDevice::ptr device, VkDescriptorPool descriptor_pool,
           VkDescriptorSetLayout descriptor_layout, VkDescriptorSet descriptor_set,
           VkPipelineLayout pipeline_layout, VkPipeline compute_pipeline,
           std::vector<ParamType> parameters)
        : device_(device),
          descriptor_pool_(descriptor_pool),
          descriptor_layout_(descriptor_layout),
          descriptor_set_(descriptor_set),
          pipeline_layout(pipeline_layout),
          compute_pipeline(compute_pipeline),
          parameters_(std::move(parameters)) {}
};

class Invoker : helpers::NonCopyable {
public:
    Invoker(Kernel::ptr kernel);
    ~Invoker();

    Invoker(Invoker&&) noexcept;
    Invoker& operator=(Invoker&&) noexcept;

public:
    Invoker& with_ubo(Buffer::ptr buffer, std::optional<size_t> offset = std::nullopt,
                      std::optional<size_t> size = std::nullopt);
    Invoker& with_ssbo(Buffer::ptr buffer, std::optional<size_t> offset = std::nullopt,
                       std::optional<size_t> size = std::nullopt);
    Invoker& with_storage_image(Texture::ptr texture, Sampler::ptr sampler,
                                std::optional<VkImageLayout> layout = std::nullopt);

public:
    void invoke(CommandBuffer::ptr cmd_buffer, glm::u64vec3 grid_size);

    void invoke(CommandBuffer::ptr cmd_buffer, size_t grid_size);

private:
    Kernel::ptr kernel_;

    struct BufferObject {
        Buffer::ptr buffer;
        size_t offset;
        size_t size;
    };
    struct StorageImage {
        Texture::ptr texture;
        Sampler::ptr sampler;
        VkImageLayout layout;
    };
    using KernelArgument = std::variant<BufferObject, StorageImage>;

    std::vector<KernelArgument> args_;
};

}  // namespace spor::vk