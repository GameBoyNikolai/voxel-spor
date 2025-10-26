#include <iostream>
#include <string>
#include <unordered_map>

#include "viewer/vulkan_application.h"
#include "viewer/test_scene.h"

int main(int argc, char** argv) { 
    spor::VulkanApplication app(argc, argv);

    auto window = app.create_window("Voxel-Spor Viewer", 1920, 1080);
    window.set_scene(std::make_unique<spor::TestScene>());

    return app.run();
}
