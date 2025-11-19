#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "viewer/glm_decl.h"
#include "viewer/vulkan_base_objects.h"
#include "viewer/vulkan_render_objects.h"

class SDL_Window;

namespace spor {

class AppWindowState;
class VulkanApplication;

enum class MouseButton {
    kNone,
    kLeft,
    kRight,
};

class CallSubmitter {
public:
    void submit_draw(vk::VulkanQueueInfo::QueueBundle queue, vk::CommandBuffer::ptr cmd_buf);
    void submit_compute(vk::VulkanQueueInfo::QueueBundle queue, vk::CommandBuffer::ptr cmd_buf);

private:
    struct Call {
        enum class Type { kDraw, kCompute };

        Type type;
        vk::VulkanQueueInfo::QueueBundle queue;
        vk::CommandBuffer::ptr cmd_buf;
    };

    std::vector<Call> submissions_;

    bool has_compute_submissions() const;

    using QueuedCalls = std::pair<vk::VulkanQueueInfo::QueueBundle, vk::CommandBuffer::ptr>;

    std::vector<QueuedCalls> get_draw_calls() const;
    std::vector<QueuedCalls> get_compute_calls() const;

    friend class AppWindowState;
};

class Scene {
public:
    virtual ~Scene() = default;

public:
    void pre_setup(vk::Instance::ptr instance, vk::SurfaceDevice::ptr surface_device,
                   vk::SwapChain::ptr swap_chain);

    virtual void setup() = 0;

    virtual void render(CallSubmitter& submitter, uint32_t framebuffer_index) = 0;

    virtual void teardown() = 0;

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