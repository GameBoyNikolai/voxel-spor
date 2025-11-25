#include "voxel/vdb.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace spor::vox {

namespace {

VDB::coord_t pos_from_index(size_t index, VDB::coord_t size) {
    return VDB::coord_t(index % size.x, (index / size.x) % size.y, index / (size.x * size.y));
}

size_t pos_to_index(VDB::coord_t pos, VDB::coord_t size) {
    return pos.x + pos.y * size.x + pos.z * size.x * size.y;
}

VDB::coord_t node_size_at_level(size_t level, VDB::coord_t base_size) {
    if (level == 0) {
        return VDB::coord_t(1);
    } else {
        return base_size * node_size_at_level(level - 1, base_size);
    }
}

template <size_t N> void pack_left(std::array<uint8_t, N>& data, uint64_t mask) {
    for (size_t i = 0, j = 0; i < N && mask != 0; ++i) {
        data[j] = data[i];
        j += mask & 1;
        mask >>= 1;
    }
}

// Build a tree and return the root node, which will be at the requested level.
// As it's based on SVNode, we hardcode 4^3=64 children per node.
SVNode generate_tree(std::vector<SVNode>& nodes, std::vector<uint8_t>& voxel_data, size_t level,
                     VDB::coord_t min, const std::function<uint8_t(VDB::coord_t)>& sampler) {
    constexpr size_t kNumChildren = sizeof(decltype(SVNode::child_mask)) * 8;
    constexpr VDB::coord_t kSize(4);

    SVNode node{};

    if (level == 1) {  // level 0 contains voxels, so level 1 nodes are leaf nodes
        node.is_leaf = true;
        node.child_offset = voxel_data.size();

        std::array<uint8_t, kNumChildren> voxels;
        for (size_t i = 0; i < kNumChildren; ++i) {
            uint8_t voxel = sampler(min + pos_from_index(i, kSize));
            if (voxel != 0) {
                node.child_mask |= 1ull << i;
                voxels[i] = voxel;
            }
        }

        pack_left(voxels, node.child_mask);
        voxel_data.insert(voxel_data.end(), voxels.begin(),
                          voxels.begin() + std::bitset<kNumChildren>(node.child_mask).count());

        return node;
    } else {
        node.is_leaf = false;

        std::vector<SVNode> children;
        children.reserve(kNumChildren);
        for (size_t i = 0; i < kNumChildren; ++i) {
            auto child_local_pos = pos_from_index(i, kSize);
            auto child = generate_tree(nodes, voxel_data, level - 1,
                                       min + child_local_pos * node_size_at_level(level - 1, kSize),
                                       sampler);

            if (child.child_mask != 0) {
                node.child_mask |= 1ull << i;

                children.push_back(child);
            }
        }

        node.child_offset = nodes.size();
        nodes.insert(nodes.end(), children.begin(), children.end());
    }

    return node;
}

double log_n(double n, double val) { return std::log(val) / std::log(n); }

}  // namespace

VDB::VDB(vk::SurfaceDevice::ptr device) : device_(device) {}

void VDB::build_from(coord_t dims, const std::function<uint8_t(coord_t)>& sampler) {
    constexpr size_t kNumChildren = sizeof(decltype(SVNode::child_mask)) * 8;
    constexpr coord_t kSize(4);

    size_t max_dim = std::max({dims.x, dims.y, dims.z});
    size_t level = std::ceil(log_n(kSize.x, max_dim));

    auto root = generate_tree(h_nodes_, h_voxels_, level, coord_t(0), sampler);
    h_nodes_.push_back(root);

    height_ = level;
    size_ = node_size_at_level(height_, kSize);
}

uint8_t VDB::get_voxel(coord_t pos) const {
    if (pos.x >= size_.x || pos.y >= size_.y || pos.z >= size_.z) {
        throw std::invalid_argument("Voxel pos is out of bounds");
    }

    if (h_nodes_.empty()) {
        throw std::runtime_error("VDB is empty");
    }

    constexpr size_t kNumChildren = sizeof(decltype(SVNode::child_mask)) * 8;
    constexpr coord_t kSize(4);

    SVNode current = h_nodes_.back();
    size_t current_level = height_;
    coord_t current_min(0);

    using MaskCounter = std::bitset<kNumChildren>;
    auto get_child_local_offset = [](const SVNode& node, size_t index) {
        // select only children mask bits *below* the one we're after
        uint64_t children_up_to_mask = (1ull << index) - 1;
        uint64_t lower_mask = node.child_mask & children_up_to_mask;

        return MaskCounter(lower_mask).count();
    };

    while (current_level >= 1) {
        auto pos_in_node = (pos - current_min) / node_size_at_level(current_level - 1, kSize);
        auto index = pos_to_index(pos_in_node, kSize);

        bool child_active = current.child_mask & (1ull << index);
        if (!child_active) {
            return 0;
        }

        auto child_index = current.child_offset + get_child_local_offset(current, index);

        if (current_level == 1) {
            return h_voxels_[child_index];
        } else {
            current = h_nodes_[child_index];
            --current_level;
            current_min += pos_in_node * node_size_at_level(current_level, kSize);
        }
    }

    throw std::runtime_error("Something went wrong while traversing node tree");
}

}  // namespace spor::vox