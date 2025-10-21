#pragma once

#include <string>
#include <memory>

namespace spor {

class Scene {
public:
    Scene() = default;
    virtual ~Scene() = default;

public:
    virtual void render() = 0;
};

class WindowBackend {
public:
    WindowBackend() = default;
    virtual ~WindowBackend() = default;

public:
    virtual void create_main_window(std::string title, size_t w, size_t h) = 0;

    virtual void draw() = 0;

    virtual bool poll_events() = 0;

    virtual void close() = 0;
};

class Window {
public:
    Window(WindowBackend* backend);

    void open();

    void set_scene(std::shared_ptr<Scene> scene);

    void run();

private:
    std::unique_ptr<WindowBackend> backend_;

    std::shared_ptr<Scene> current_scene_;
};

}  // namespace spor