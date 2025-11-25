#include "viewer/vulkan_application.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

#include "SDL3/SDL.h"
#include "SDL3/SDL_vulkan.h"
#include "fmt/format.h"
#include "shaders/test.frag.inl"
#include "shaders/test.vert.inl"
#include "vkh/glm_decl.h"
#include "vkh/render_objects.h"
#include "vulkan/vulkan.h"

namespace spor {

void check_sdl(int code) {
    if (code < 0) {
        const char* err = SDL_GetError();
        std::cerr << fmt::format("SDL Error: {}", err) << std::endl;
        throw std::runtime_error(fmt::format("SDL Error: %s", err));
    }
}

void Scene::pre_setup(vk::Instance::ptr instance, vk::SurfaceDevice::ptr surface_device,
                      vk::SwapChain::ptr swap_chain) {
    instance_ = instance;
    surface_device_ = surface_device;
    swap_chain_ = swap_chain;
}

class AppWindowState {
public:
    AppWindowState(std::shared_ptr<vk::WindowHandle> window, vk::Instance::ptr instance,
                   vk::SurfaceDevice::ptr device)
        : instance_(instance),
          window_(window),
          device_(device),
          swap_chain_ready_(vk::Semaphore::create(device)) {
        int pixel_w, pixel_h;
        SDL_GetWindowSizeInPixels(*window_, &pixel_w, &pixel_h);
        swap_chain_ = vk::SwapChain::create(device_, static_cast<uint32_t>(pixel_w),
                                            static_cast<uint32_t>(pixel_h));

        base_title_ = SDL_GetWindowTitle(*window_);
        time_ = static_cast<double>(SDL_GetPerformanceCounter()) / SDL_GetPerformanceFrequency();
    }

    ~AppWindowState() {
        if (scene_) {
            scene_->teardown();
        }
        vkDeviceWaitIdle(device_->device);
    }

public:
    void draw() {
        // resize swap chain?

        scene_->block_for_current_frame();

        uint32_t image_index;
        vkAcquireNextImageKHR(device_->device, swap_chain_->swap_chain,
                              std::numeric_limits<uint64_t>::max(), *swap_chain_ready_,
                              VK_NULL_HANDLE, &image_index);

        auto frame_finished = scene_->render(image_index, swap_chain_ready_);

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &frame_finished->semaphore;

        VkSwapchainKHR swap_chains[] = {swap_chain_->swap_chain};
        present_info.swapchainCount = 1;
        present_info.pSwapchains = swap_chains;
        present_info.pImageIndices = &image_index;

        present_info.pResults = nullptr;  // Optional

        vkQueuePresentKHR(device_->queues.present.queue, &present_info);

        double current_time
            = static_cast<double>(SDL_GetPerformanceCounter()) / SDL_GetPerformanceFrequency();
        std::string new_title = base_title_ + " | " + std::to_string(1.0 / (current_time - time_));
        SDL_SetWindowTitle(*window_, new_title.data());

        time_ = current_time;
    }

public:
    void on_mouse_down(MouseButton button, glm::vec2 pos) {
        last_mouse_pos_[button] = pos;

        if (scene_) {
            scene_->on_mouse_down(button);
        }
    }
    void on_mouse_up(MouseButton button, glm::vec2 pos) {
        if (scene_) {
            scene_->on_mouse_up(button);
        }
    }

    void on_mouse_drag(MouseButton button, glm::vec2 pos) {
        if (scene_) {
            scene_->on_mouse_drag(button, pos - last_mouse_pos_[button]);
        }

        last_mouse_pos_[button] = pos;
    }

    void on_mouse_scroll(float amount) {
        if (scene_) {
            scene_->on_mouse_scroll(amount);
        }
    }

public:
    void set_scene(std::unique_ptr<Scene> scene) {
        if (scene_) {
            scene_->teardown();
        }

        scene_ = std::move(scene);
        scene_->pre_setup(instance_, device_, swap_chain_);
        scene_->setup();
    }

    std::shared_ptr<vk::WindowHandle> window() { return window_; }

    void request_close() { requesting_close_ = true; }

    bool is_requesting_close() const { return requesting_close_; }

private:
    vk::Instance::ptr instance_;

    std::shared_ptr<vk::WindowHandle> window_;
    vk::SurfaceDevice::ptr device_;
    vk::SwapChain::ptr swap_chain_;

    bool requesting_close_{false};

    // Scene state
    vk::Semaphore::ptr swap_chain_ready_;

    std::unique_ptr<Scene> scene_{nullptr};

    std::unordered_map<MouseButton, glm::vec2> last_mouse_pos_;

    std::string base_title_ = "";
    double time_ = 0.f;
};

Window::Window(AppWindowState* state) : state_(state) {}

void Window::set_scene(std::unique_ptr<Scene> scene) { state_->set_scene(std::move(scene)); }

void Window::close() { state_->request_close(); }

VulkanApplication::VulkanApplication(int, char**) {
    check_sdl(SDL_Init(SDL_INIT_VIDEO));
    check_sdl(SDL_Vulkan_LoadLibrary(nullptr));

    instance_ = vk::Instance::create("Vulkan Instance");
}

VulkanApplication::~VulkanApplication() = default;

Window VulkanApplication::create_window(std::string title, size_t w, size_t h) {
    SDL_WindowFlags window_flags = SDL_WINDOW_VULKAN;
    auto* window
        = SDL_CreateWindow(title.data(), static_cast<int>(w), static_cast<int>(h), window_flags);
    if (!window) {
        check_sdl(-1);
    }

    std::set<std::string> required_extensions = {
        std::string(VK_KHR_SWAPCHAIN_EXTENSION_NAME),
    };

    auto window_handle = std::make_shared<vk::WindowHandle>(window);
    auto surface_device = vk::SurfaceDevice::create(instance_, window_handle, required_extensions);
    window_states_.push_back(
        std::make_unique<AppWindowState>(window_handle, instance_, surface_device));

    window_to_state_[window] = window_states_.back().get();

    return Window(window_states_.back().get());
}

int VulkanApplication::run() {
    bool any_window_open = true;
    bool quit = false;

    std::set<SDL_Window*> windows_to_kill;

    while (any_window_open && !quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            // close the window when user clicks the X button or alt-f4s
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    quit = true;
                    break;
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    windows_to_kill.insert(SDL_GetWindowFromID(event.window.windowID));
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    auto* w_state = window_to_state_[SDL_GetWindowFromID(event.button.windowID)];

                    // only one mouse button can be down in a single event (AFAIK)
                    MouseButton button = MouseButton::kNone;
                    if (event.button.button & SDL_BUTTON_LMASK) {
                        button = MouseButton::kLeft;
                    } else if (event.button.button & SDL_BUTTON_RMASK) {
                        button = MouseButton::kRight;
                    }

                    if (button != MouseButton::kNone) {
                        if (event.button.down) {
                            w_state->on_mouse_down(button,
                                                   glm::vec2(event.button.x, event.button.y));
                        } else {
                            w_state->on_mouse_up(button, glm::vec2(event.button.x, event.button.y));
                        }
                    }
                } break;
                case SDL_EVENT_MOUSE_MOTION: {
                    auto* w_state = window_to_state_[SDL_GetWindowFromID(event.button.windowID)];

                    // multiple mouse buttons can participate in a drag
                    if (event.button.button & SDL_BUTTON_LMASK) {
                        w_state->on_mouse_drag(MouseButton::kLeft,
                                               glm::vec2(event.button.x, event.button.y));
                    }

                    if (event.button.button & SDL_BUTTON_RMASK) {
                        w_state->on_mouse_drag(MouseButton::kRight,
                                               glm::vec2(event.button.x, event.button.y));
                    }
                } break;
                case SDL_EVENT_MOUSE_WHEEL: {
                    auto* w_state = window_to_state_[SDL_GetWindowFromID(event.wheel.windowID)];

                    w_state->on_mouse_scroll(event.wheel.y);
                } break;
            }
        }

        std::set<AppWindowState*> states_to_cull;
        for (auto& w_state : window_states_) {
            if (w_state->is_requesting_close()) {
                states_to_cull.insert(w_state.get());
                continue;
            }

            w_state->draw();
        }

        for (auto* window : windows_to_kill) {
            states_to_cull.insert(window_to_state_[window]);
            window_to_state_.erase(window);
        }

        window_states_.erase(std::remove_if(window_states_.begin(), window_states_.end(),
                                            [&states_to_cull](const auto& element) {
                                                return states_to_cull.count(element.get()) > 0;
                                            }),
                             window_states_.end());

        windows_to_kill.clear();

        any_window_open = !window_states_.empty();
    }

    return 0;
}

}  // namespace spor