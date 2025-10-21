#include <iostream>
#include <string>
#include <unordered_map>

#include "voxel_spor/window.h"
#include "voxel_spor/vulkan_backend.h"

int main(int, char**) { 
    spor::Window window(new spor::VulkanBackend()); 
    window.open();
    window.run();

    return 0;
}
