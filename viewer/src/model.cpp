#include "viewer/model.h"

#include <fstream>
#include <unordered_map>
#include <vector>

#include "stb_image.h"
#include "tiny_obj_loader.h"
#include "vkh/glm_decl.h"

namespace spor::vk {

namespace helpers {
vk::Texture::ptr load_texture(vk::SurfaceDevice::ptr device, vk::CommandPool::ptr pool,
                              VkQueue queue, const std::string& path) {
    int width, height, channels;
    stbi_uc* image_data = stbi_load(path.data(), &width, &height, &channels, STBI_rgb_alpha);

    auto transfer_buf = vk::create_and_fill_transfer_buffer(
        device, image_data, static_cast<size_t>(width * height * STBI_rgb_alpha));

    stbi_image_free(image_data);

    auto texture = vk::Texture::create(device, width, height);
    vk::submit_commands(vk::transition_texture(device, pool, texture, VK_IMAGE_LAYOUT_UNDEFINED,
                                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
                        queue);

    vk::submit_commands(vk::texture_memcpy(device, pool, transfer_buf, texture), queue);

    vk::submit_commands(
        vk::transition_texture(device, pool, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
        queue);

    return texture;
}
}  // namespace helpers

struct ModelVertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 uv;

    bool operator==(const ModelVertex& other) const {
        return pos == other.pos && color == other.color && uv == other.uv;
    }

    static VkVertexInputBindingDescription binding_description() {
        VkVertexInputBindingDescription desc{};
        desc.binding = 0;
        desc.stride = sizeof(ModelVertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return desc;
    }

    static std::vector<VkVertexInputAttributeDescription> attribute_descriptions() {
        std::vector<VkVertexInputAttributeDescription> descs(3);

        descs[0].binding = 0;
        descs[0].location = 0;
        descs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        descs[0].offset = offsetof(ModelVertex, pos);

        descs[1].binding = 0;
        descs[1].location = 1;
        descs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        descs[1].offset = offsetof(ModelVertex, color);

        descs[2].binding = 0;
        descs[2].location = 2;
        descs[2].format = VK_FORMAT_R32G32_SFLOAT;
        descs[2].offset = offsetof(ModelVertex, uv);
        descs[2].offset = offsetof(ModelVertex, uv);

        return descs;
    }
};
}  // namespace spor::vk

namespace std {

template <> struct hash<spor::vk::ModelVertex> {
    size_t operator()(const spor::vk::ModelVertex& v) const {
        return (hash<decltype(v.pos)>()(v.pos) << 16)       //
               ^ (hash<decltype(v.color)>()(v.color) << 8)  //
               ^ (hash<decltype(v.uv)>()(v.uv) << 4);
    }
};
}  // namespace std

namespace spor::vk {

Model::ptr Model::from_obj(SurfaceDevice::ptr device, CommandPool::ptr cmd_pool,
                           const fs::path& obj_path, const fs::path& tex_path) {
    tinyobj::attrib_t attributes;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err_msg;

    std::ifstream obj_in(obj_path);
    if (!obj_in) {
        throw std::runtime_error("failed to open mode file: " + obj_path.generic_string());
    }

    if (!tinyobj::LoadObj(&attributes, &shapes, &materials, &err_msg, &obj_in)) {
        throw std::runtime_error("failed to load model: " + err_msg);
    }

    std::unordered_map<ModelVertex, size_t> vertex_to_index;
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;

    for (const auto& shape : shapes) {
        for (const auto& mesh_index : shape.mesh.indices) {
            ModelVertex vertex;
            vertex.pos = {attributes.vertices[3 * mesh_index.vertex_index + 0],
                          attributes.vertices[3 * mesh_index.vertex_index + 1],
                          attributes.vertices[3 * mesh_index.vertex_index + 2]};

            // flip UV for vulkan
            vertex.uv = {attributes.texcoords[2 * mesh_index.texcoord_index + 0],
                         1.f - attributes.texcoords[2 * mesh_index.texcoord_index + 1]};

            vertex.color = {1.0f, 1.0f, 1.0f};

            size_t index;
            if (vertex_to_index.count(vertex) == 0) {
                vertices.push_back(vertex);
                index = vertices.size() - 1;
                vertex_to_index[vertex] = index;
            } else {
                index = vertex_to_index[vertex];
            }
            indices.push_back(index);
        }
    }

    auto vbo = create_vertex_buffer(device, vertices.size(), sizeof(decltype(vertices.front())));
    auto ibo = create_index_buffer(device, indices.size(), sizeof(decltype(indices.front())));

    auto graphics_queue = device->queues.graphics.queue;
    {
        auto transfer_buf = create_and_fill_transfer_buffer(device, vertices);
        auto transfer_cmd = buffer_memcpy(device, cmd_pool, transfer_buf, vbo, vbo->size());
        vk::submit_commands(transfer_cmd, graphics_queue);
    }

    {
        auto transfer_buf = create_and_fill_transfer_buffer(device, indices);
        auto transfer_cmd = buffer_memcpy(device, cmd_pool, transfer_buf, ibo, ibo->size());
        vk::submit_commands(transfer_cmd, graphics_queue);
    }

    auto texture
        = helpers::load_texture(device, cmd_pool, graphics_queue,
                                         tex_path.generic_string());

    return std::make_shared<Model>(PrivateToken{}, device, vbo, ibo, texture);
}

VkVertexInputBindingDescription Model::vertex_binding_description() {
    return ModelVertex::binding_description();
}

std::vector<VkVertexInputAttributeDescription> Model::vertex_attribute_descriptions() {
    return ModelVertex::attribute_descriptions();
}

void Model::draw(CommandBuffer::ptr cmd_buffer, DescriptorSet descriptors,
                 GraphicsPipeline::ptr pipeline) {
    vkCmdBindDescriptorSets(cmd_buffer->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->pipeline_layout, 1, 1,
                            &descriptors.descriptor_set, 0, nullptr);

    VkBuffer vertex_buffers[] = {vbo_->buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd_buffer->command_buffer, 0, 1, vertex_buffers, offsets);

    vkCmdBindIndexBuffer(cmd_buffer->command_buffer, ibo_->buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(cmd_buffer->command_buffer, static_cast<uint32_t>(ibo_->element_count), 1, 0,
                     0, 0);
}

}  // namespace spor::vk