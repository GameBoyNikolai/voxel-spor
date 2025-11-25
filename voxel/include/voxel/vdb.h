#pragma once

#include <functional>
#include <vector>

#include "vkh/glm_decl.h"
#include "vkh/base_objects.h"
#include "vkh/buffer_objects.h"

namespace spor::vox {

#pragma pack(4)
struct SVNode {
    uint32_t is_leaf : 1;
    uint32_t child_offset{0};
    uint64_t child_mask{0};
};
#pragma pack()
static_assert(sizeof(SVNode) == 16, "SVNode is not properly packed/aligned");

class VDB {
public:
    using coord_t = glm::u32vec3;
public:
    VDB(vk::SurfaceDevice::ptr device);

public:
    void build_from(coord_t dims, const std::function<uint8_t(coord_t)>& sampler);

public:
    uint8_t get_voxel(coord_t pos) const;

private:
    friend class TestInspector;

    vk::SurfaceDevice::ptr device_;

    size_t height_{0};
    coord_t size_{0};

    // host
    std::vector<SVNode> h_nodes_;
    std::vector<uint8_t> h_voxels_;

    // device
    vk::Buffer::ptr d_nodes_;
    vk::Buffer::ptr d_voxels_;
};

}  // namespace spor::vox