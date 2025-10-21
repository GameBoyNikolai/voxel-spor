#pragma once

#include <memory>

#include "voxel_spor/window.h"

namespace spor {

class VulkanBackend : public WindowBackend {
public:
    VulkanBackend();
    virtual ~VulkanBackend();

public:
    virtual void create_main_window(std::string title, size_t w, size_t h) override;

    virtual bool poll_events() override;

    virtual void draw() override;

    virtual void close() override;

private:
    struct ImplState;
    std::unique_ptr<ImplState> impl_;
};

}  // namespace spor