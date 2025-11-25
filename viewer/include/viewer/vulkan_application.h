#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "vkh/glm_decl.h"
#include "vkh/base_objects.h"
#include "vkh/render_objects.h"

class SDL_Window;

namespace spor {

class AppWindowState;
class VulkanApplication;

enum class MouseButton {
    kNone,
    kLeft,
    kRight,
};

class Scene {
public:
    virtual ~Scene() = default;

public:
    void pre_setup(vk::Instance::ptr instance, vk::SurfaceDevice::ptr surface_device,
                   vk::SwapChain::ptr swap_chain);

    virtual void setup() = 0;

    virtual vk::Semaphore::ptr render(uint32_t framebuffer_index, vk::Semaphore::ptr swap_chain_available) = 0;

    virtual void teardown() = 0;

public:
    virtual void block_for_current_frame() = 0;

public:
    virtual void on_mouse_down(MouseButton button) {}
    virtual void on_mouse_up(MouseButton button) {}

    virtual void on_mouse_drag(MouseButton button, glm::vec2 offset) {}

    virtual void on_mouse_scroll(float offset) {}

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
    std::unordered_map<SDL_Window*, AppWindowState*> window_to_state_;
};

}  // namespace spor