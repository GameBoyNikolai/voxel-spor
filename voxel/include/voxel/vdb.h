#pragma once

#include <functional>
#include <vector>

#include "vkh/base_objects.h"
#include "vkh/buffer_objects.h"
#include "vkh/glm_decl.h"

namespace spor::vox {

#pragma pack(4)
struct SVNode {
    uint32_t is_leaf : 1;
    uint32_t child_offset : 31;
    uint64_t child_mask{0};
};
#pragma pack()
static_assert(sizeof(SVNode) == 12, "SVNode is not properly packed/aligned");

#pragma pack(4)
struct VDBInfo {
    glm::uvec3 size;
    glm::u32 height;
};
#pragma pack()
static_assert(sizeof(VDBInfo) == 16, "VDBInfo is not properly packed/aligned");

class VDB {
public:
    using coord_t = glm::uvec3;

public:
    VDB(vk::SurfaceDevice::ptr device);

public:
    void build_from(coord_t dims, const std::function<uint8_t(coord_t)>& sampler);

    void move_to_device(vk::CommandPool::ptr cmd_pool);

    vk::Buffer::ptr info_buffer() { return d_info_; }
    vk::Buffer::ptr node_buffer() { return d_nodes_; }
    vk::Buffer::ptr voxel_buffer() { return d_voxels_; }

public:
    uint8_t get_voxel(coord_t pos) const;

public:
    size_t height() { return height_; }
    coord_t size() { return size_; }

private:
    friend class TestInspector;

    vk::SurfaceDevice::ptr device_;

    size_t height_{0};
    coord_t size_{0};

    // host
    std::vector<SVNode> h_nodes_;
    std::vector<uint8_t> h_voxels_;

    // device
    vk::Buffer::ptr d_info_;
    vk::Buffer::ptr d_nodes_;
    vk::Buffer::ptr d_voxels_;
};

}  // namespace spor::vox