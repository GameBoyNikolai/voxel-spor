#include "gtest/gtest.h"
#include "voxel/vdb.h"

namespace spor::vox {
class TestInspector {
public:
    TestInspector(const VDB& vdb) : vdb_(vdb) {}

    const std::vector<SVNode>& get_nodes() { return vdb_.h_nodes_; }

    const std::vector<uint8_t>& get_voxels() { return vdb_.h_voxels_; }

private:
    const VDB& vdb_;
};
}  // namespace spor::vox

using namespace spor;

TEST(TestTreeBuild, Solid) {
    auto sampler = [](vox::VDB::coord_t pos) { return static_cast<uint8_t>(1); };

    {
        vox::VDB vdb(nullptr);
        vdb.build_from(vox::VDB::coord_t(4, 4, 4), sampler);
        spor::vox::TestInspector i(vdb);
        EXPECT_EQ(i.get_nodes().size(), 1);
        EXPECT_EQ(i.get_voxels().size(), 4 * 4 * 4);

        for (size_t z = 0; z < 4; ++z) {
            for (size_t y = 0; y < 4; ++y) {
                for (size_t x = 0; x < 4; ++x) {
                    EXPECT_EQ(vdb.get_voxel({x, y, z}), 1);
                }
            }
        }
    }

    {
        vox::VDB vdb(nullptr);
        vdb.build_from(vox::VDB::coord_t(16, 16, 16), sampler);
        spor::vox::TestInspector i(vdb);
        EXPECT_EQ(i.get_nodes().size(), 1 + 64);
        EXPECT_EQ(i.get_voxels().size(), 16 * 16 * 16);

        // a 2 level tree will have the root's leaves starting at 1
        EXPECT_EQ(i.get_nodes().front().child_offset, 1);

        for (size_t z = 0; z < 16; ++z) {
            for (size_t y = 0; y < 16; ++y) {
                for (size_t x = 0; x < 16; ++x) {
                    EXPECT_EQ(vdb.get_voxel({x, y, z}), 1) << x << " " << y << " " << z;
                }
            }
        }
    }
}

TEST(TestTreeBuild, SolidNumbered) {
    {
        auto sampler = [](vox::VDB::coord_t pos) {
            return static_cast<uint8_t>(pos.x + 4 * pos.y + 4 * 4 * pos.z + 1);
        };

        vox::VDB vdb(nullptr);
        vdb.build_from(vox::VDB::coord_t(4, 4, 4), sampler);
        spor::vox::TestInspector i(vdb);
        EXPECT_EQ(i.get_nodes().size(), 1);
        EXPECT_EQ(i.get_voxels().size(), 4 * 4 * 4);

        for (size_t z = 0; z < 4; ++z) {
            for (size_t y = 0; y < 4; ++y) {
                for (size_t x = 0; x < 4; ++x) {
                    EXPECT_EQ(vdb.get_voxel({x, y, z}), x + 4 * y + 4 * 4 * z + 1);
                }
            }
        }
    }

    {
        auto sampler
            = [](vox::VDB::coord_t pos) { return static_cast<uint8_t>(pos.x + pos.y + pos.z + 1); };

        vox::VDB vdb(nullptr);
        vdb.build_from(vox::VDB::coord_t(16, 16, 16), sampler);
        spor::vox::TestInspector i(vdb);
        EXPECT_EQ(i.get_nodes().size(), 1 + 64);
        EXPECT_EQ(i.get_voxels().size(), 16 * 16 * 16);

        // a 2 level tree will have the root's leaves starting at 1
        EXPECT_EQ(i.get_nodes().front().child_offset, 1);

        for (size_t z = 0; z < 16; ++z) {
            for (size_t y = 0; y < 16; ++y) {
                for (size_t x = 0; x < 16; ++x) {
                    EXPECT_EQ(vdb.get_voxel({x, y, z}), x + y + z + 1);
                }
            }
        }
    }
}

TEST(TestTreeBuild, TestSphere) {
    constexpr size_t kRadius = 10.f;
    constexpr size_t kSize = 64;

    const vox::VDB::coord_t center(kSize / 2);

    auto sampler = [center, kRadius](vox::VDB::coord_t pos) -> uint8_t {
        int dist = static_cast<int>(glm::distance(glm::vec3(center), glm::vec3(pos)));
        if (dist > kRadius) {
            return 0;
        } else {
            return dist + 1;
        }
    };

    vox::VDB vdb(nullptr);
    vdb.build_from(vox::VDB::coord_t(kSize), sampler);

    if constexpr (false) {
        for (size_t z = 0; z < kSize; ++z) {
            for (size_t y = 0; y < kSize; ++y) {
                for (size_t x = 0; x < kSize; ++x) {
                    std::cout << (vdb.get_voxel({x, y, z}) > 0 ? '*' : ' ') << " ";
                }
                std::cout << std::endl;
            }
            std::cout << std::endl;
        }
    }

    for (size_t z = 0; z < kSize; ++z) {
        for (size_t y = 0; y < kSize; ++y) {
            for (size_t x = 0; x < kSize; ++x) {
                auto val = vdb.get_voxel({x, y, z});
                EXPECT_EQ(val, sampler({x, y, z}));

                if (val > 0) {
                    EXPECT_LE(static_cast<int>(glm::distance(glm::vec3(center), glm::vec3(x, y, z))),
                              kRadius);
                }
            }
        }
    }
}

TEST(TestTreeBuild, TestSphereLarge) {
    constexpr size_t kRadius = 105.f;
    constexpr size_t kSize = 256;

    const vox::VDB::coord_t center(kSize / 2);

    auto sampler = [center, kRadius](vox::VDB::coord_t pos) -> uint8_t {
        int dist = static_cast<int>(glm::distance(glm::vec3(center), glm::vec3(pos)));
        if (dist > kRadius) {
            return 0;
        } else {
            return dist + 1;
        }
    };

    vox::VDB vdb(nullptr);
    vdb.build_from(vox::VDB::coord_t(kSize), sampler);

    if constexpr (false) {
        for (size_t z = 0; z < kSize; ++z) {
            for (size_t y = 0; y < kSize; ++y) {
                for (size_t x = 0; x < kSize; ++x) {
                    std::cout << (vdb.get_voxel({x, y, z}) > 0 ? '*' : ' ') << " ";
                }
                std::cout << std::endl;
            }
            std::cout << std::endl;
        }
    }

    for (size_t z = 0; z < kSize; ++z) {
        for (size_t y = 0; y < kSize; ++y) {
            for (size_t x = 0; x < kSize; ++x) {
                auto val = vdb.get_voxel({x, y, z});
                EXPECT_EQ(val, sampler({x, y, z}));

                if (val > 0) {
                    EXPECT_LE(static_cast<int>(glm::distance(glm::vec3(center), glm::vec3(x, y, z))),
                              kRadius);
                }
            }
        }
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}