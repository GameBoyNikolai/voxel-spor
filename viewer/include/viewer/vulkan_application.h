#pragma once

#include <memory>
#include <string>

#include "vulkan_objects.h"

namespace spor {

class AppWindowState;
class VulkanApplication;

class Scene {
public:
    virtual ~Scene() = default;

public:
    void pre_setup(vk::Instance::ptr instance, vk::SurfaceDevice::ptr surface_device,
               vk::SwapChain::ptr swap_chain);

    virtual void setup() = 0;

    virtual vk::CommandBuffer::ptr render(uint32_t framebuffer_index) = 0;

    virtual void teardown() = 0;

protected:
    vk::Instance::ptr instance_;
    vk::SurfaceDevice::ptr surface_device_;
    vk::SwapChain::ptr swap_chain_;
};

class Window {
private:
    friend class VulkanApplication;

    Window(AppWindowState* state);

public:
    ~Window() = default;

    Window(const Window&) = default;
    Window(Window&&) = default;

    Window& operator=(const Window&) = default;
    Window& operator=(Window&&) = default;

public:
    void set_scene(std::unique_ptr<Scene> scene);

    void close();

private:
    AppWindowState* state_;
};

class VulkanApplication {
public:
    VulkanApplication(int argc, char** argv);
    ~VulkanApplication();

public:
    Window create_window(std::string title, size_t w, size_t h);

    int run();

private:
    vk::Instance::ptr instance_;

    std::vector<std::unique_ptr<AppWindowState>> window_states_;
};

}  // namespace spor