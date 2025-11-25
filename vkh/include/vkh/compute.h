#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "vkh/glm_decl.h"
#include "vkh/base_objects.h"
#include "vkh/buffer_objects.h"
#include "vkh/render_objects.h"
#include "vkh/helpers.h"
#include "vulkan/vulkan.h"

namespace spor::vk {

class Kernel : public helpers::VulkanObject<Kernel> {
public:
    ~Kernel();

public:
    friend class Invoker;

    operator VkPipeline() const { return compute_pipeline; };

    VkDescriptorSetLayout parameter_layout() const { return descriptor_layout_; }

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
    void invoke(CommandBuffer::ptr cmd_buffer, DescriptorSet args, glm::u64vec3 grid_size);
    void invoke(CommandBuffer::ptr cmd_buffer, DescriptorSet args, size_t grid_size);

public:
    VkPipelineLayout pipeline_layout;
    VkPipeline compute_pipeline;

private:
    SurfaceDevice::ptr device_;

    VkDescriptorSetLayout descriptor_layout_;

    std::vector<ParamType> parameters_;

public:
    Kernel(PrivateToken, SurfaceDevice::ptr device, VkDescriptorSetLayout descriptor_layout,
           VkPipelineLayout pipeline_layout, VkPipeline compute_pipeline,
           std::vector<ParamType> parameters)
        : device_(device),
          descriptor_layout_(descriptor_layout),
          pipeline_layout(pipeline_layout),
          compute_pipeline(compute_pipeline),
          parameters_(std::move(parameters)) {}
};

}  // namespace spor::vk