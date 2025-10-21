#include "voxel_spor/window.h"

namespace spor {

Window::Window(WindowBackend* backend) : backend_(backend) {}

void Window::open() { backend_->create_main_window("Spor Viewer", 1920, 1080); }

void Window::set_scene(std::shared_ptr<Scene> scene) { current_scene_ = scene; }

void Window::run() {
    while (backend_->poll_events()) {
        backend_->draw();
    }

    backend_->close();
}

}  // namespace spor