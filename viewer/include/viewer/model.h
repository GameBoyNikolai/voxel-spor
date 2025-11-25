#pragma once

#include <filesystem>

#include "vkh/glm_decl.h"
#include "vkh/buffer_objects.h"
#include "vkh/render_objects.h"
#include "vkh/helpers.h"

namespace spor::vk {

namespace fs = std::filesystem;

class Model : public helpers::VulkanObject<Model> {
public:
    static ptr from_obj(SurfaceDevice::ptr device, CommandPool::ptr cmd_pool, const fs::path& obj_path, const fs::path& tex_path);

public:
    VkVertexInputBindingDescription vertex_binding_description();
    std::vector<VkVertexInputAttributeDescription> vertex_attribute_descriptions();

    Texture::ptr texture() { return texture_; }

    void draw(CommandBuffer::ptr cmd_buffer, DescriptorSet descriptors,
              GraphicsPipeline::ptr pipeline);

public:
    Model(PrivateToken, SurfaceDevice::ptr device, Buffer::ptr vbo, Buffer::ptr ibo, Texture::ptr texture)
        : device_(device), vbo_(vbo), ibo_(ibo), texture_(texture) {}

public:
    glm::mat4 xfm = glm::mat4(1.0f);

private:
    SurfaceDevice::ptr device_;

    Buffer::ptr vbo_;
    Buffer::ptr ibo_;
    Texture::ptr texture_;
};

}  // namespace spor::vk