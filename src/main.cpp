#include <cstdlib>
#include <vector>

#include "app/App.hpp"
#include "config.hpp"
#include "logger.hpp"

int main(int argc, char** argv)
{
    if (argc >= 2 && std::string(argv[1]) == "--validate-config") {
        std::string filename = "overlay.json";
        if (argc >= 3) {
            filename = argv[2];
        }
        std::string error_msg;
        if (validate_config(filename, error_msg)) {
            FC2_LOG_INFO("Config file '{}' is valid.", filename);
            return 0;
        } else {
            FC2_LOG_ERROR("Config validation failed: {}", error_msg);
            return 1;
        }
    }

    FC2_LOG_INFO("Launching GPU-Skinned Linux Overlay...");

    OverlayConfig cfg = load_config("overlay.json");

    // Apply GPU offload settings before GLFW/OpenGL driver initialization
    if (cfg.gpu_preference != "default" && !cfg.gpu_preference.empty()) {
        std::vector<GpuDevice> gpus = detect_gpus();
        const GpuDevice* selected_gpu = nullptr;
        for (const auto& gpu : gpus) {
            if (gpu.name == cfg.gpu_preference) {
                selected_gpu = &gpu;
                break;
            }
        }
        if (selected_gpu) {
            FC2_LOG_INFO("Applying GPU preference: {}", selected_gpu->display_name);
            if (selected_gpu->vendor_id == "0x10de" || selected_gpu->vendor_id == "10de" ||
                selected_gpu->driver == "nvidia") {
                setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 1);
                setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 1);
                setenv("__VK_LAYER_NV_optimus", "NVIDIA_only", 1);
                unsetenv("DRI_PRIME");
            } else {
                // AMD / Intel offload via DRI_PRIME
                setenv("DRI_PRIME", selected_gpu->name.c_str(), 1);
                unsetenv("__NV_PRIME_RENDER_OFFLOAD");
                unsetenv("__GLX_VENDOR_LIBRARY_NAME");
                unsetenv("__VK_LAYER_NV_optimus");
            }
        }
    } else {
        // Clear offload environment variables for default setting
        unsetenv("__NV_PRIME_RENDER_OFFLOAD");
        unsetenv("__GLX_VENDOR_LIBRARY_NAME");
        unsetenv("__VK_LAYER_NV_optimus");
        unsetenv("DRI_PRIME");
    }

    App app(cfg);
    return app.run();
}
