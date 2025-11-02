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
#include "vulkan/vulkan.h"

#include "viewer/vulkan_render_objects.h"

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
          sync_objects_(vk::DefaultRenderSyncObjects::create(device)) {
        int pixel_w, pixel_h;
        SDL_GetWindowSizeInPixels(*window_, &pixel_w, &pixel_h);
        swap_chain_ = vk::SwapChain::create(device_, static_cast<uint32_t>(pixel_w),
                                            static_cast<uint32_t>(pixel_h));
    }

    ~AppWindowState() {
        vkDeviceWaitIdle(device_->device);
    }

public:
    void draw() {
        // resize swap chain?

        vkWaitForFences(device_->device, 1, &sync_objects_->in_flight, VK_TRUE,
                        std::numeric_limits<uint64_t>::max());
        vkResetFences(device_->device, 1, &sync_objects_->in_flight);

        uint32_t image_index;
        vkAcquireNextImageKHR(device_->device, swap_chain_->swap_chain,
                              std::numeric_limits<uint64_t>::max(), sync_objects_->image_available,
                              VK_NULL_HANDLE, &image_index);

        auto cmd_buffer = scene_->render(image_index);

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore wait_semaphores[] = {sync_objects_->image_available};
        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = wait_semaphores;
        submit_info.pWaitDstStageMask = wait_stages;

        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd_buffer->command_buffer;

        VkSemaphore signal_semaphores[] = {sync_objects_->render_finished};
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = signal_semaphores;

        vk::helpers::check_vulkan(vkQueueSubmit(*device_->queues.graphics_queue, 1, &submit_info,
                                   sync_objects_->in_flight));

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = signal_semaphores;

        VkSwapchainKHR swap_chains[] = {swap_chain_->swap_chain};
        present_info.swapchainCount = 1;
        present_info.pSwapchains = swap_chains;
        present_info.pImageIndices = &image_index;

        present_info.pResults = nullptr;  // Optional

        vkQueuePresentKHR(*device_->queues.present_queue, &present_info);
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
    vk::DefaultRenderSyncObjects::ptr sync_objects_;

    std::unique_ptr<Scene> scene_{nullptr};
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

    return Window(window_states_.back().get());
}

int VulkanApplication::run() {
    bool any_window_open = true;
    bool quit = false;
    while (any_window_open && !quit) {
        std::set<SDL_Window*> windows_to_kill;

        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            // close the window when user clicks the X button or alt-f4s
            if (event.type == SDL_EVENT_QUIT) {
                quit = true;
                break;
            } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                windows_to_kill.insert(SDL_GetWindowFromID(event.window.windowID));
            }
        }

        std::set<AppWindowState*> states_to_cull;
        for (auto& w_state : window_states_) {
            if (windows_to_kill.count(*w_state->window())) {
                states_to_cull.insert(w_state.get());
                continue;
            }

            if (w_state->is_requesting_close()) {
                states_to_cull.insert(w_state.get());
                continue;
            }

            w_state->draw();
        }

        window_states_.erase(std::remove_if(window_states_.begin(), window_states_.end(),
                                            [&states_to_cull](const auto& element) {
                                                return states_to_cull.count(element.get()) > 0;
                                            }),
                             window_states_.end());

        any_window_open = !window_states_.empty();
    }

    return 0;
}

}  // namespace spor