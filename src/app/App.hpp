#pragma once
#include <memory>
#include <string>
#include <future>
#include <chrono>
#include "config.hpp"
#include "overlay/overlay_client.hpp"
#include "menu/menu_client.hpp"
#include "renderer/gpu_chams.hpp"
#include "renderer/depth_prepass.hpp"
#include "renderer/esp_renderer.hpp"
#include "model_cache.hpp"
#include "overlay/shm_reader.hpp"
#include "VisibilityWorker.hpp"
#include "GrenadeTracker.hpp"
#include "FrameScheduler.hpp"
#include "FrameInput.hpp"
#include "FrameState.hpp"
#include "renderer/gpu_profiler.hpp"

class App {
public:
    App(const OverlayConfig& initial_cfg);
    ~App();

    int run();

private:
    bool initialize_system();
    void process_frame();
    void update_map_loading();
    void prepare_frame_input(FrameInput& input);
    void preprocess_frame_state(const FrameInput& input, FrameState& state);
    void render_frame(const FrameInput& input, FrameState& state);
    void render_menu();

    OverlayConfig cfg;
    std::unique_ptr<OverlayClient> overlay;
    std::unique_ptr<MenuClient> menu;

    GpuChamsRenderer chams_renderer;
    DepthPrepassRenderer depth_prepass;
    EspRenderer esp_renderer;
    ModelCache model_cache;
    ShmReader shm;

    VisibilityWorker visibility_worker;
    GrenadeTracker grenade_tracker;
    FrameScheduler scheduler;
    GpuProfiler gpu_profiler;

    // Window dimensions and scaling geometries
    int window_width = 0;
    int window_height = 0;
    int window_x = 0;
    int window_y = 0;

    // IPC performance metrics
    double total_ipc_time_ms = 0.0;
    double total_cpu_time_ms = 0.0;
    double total_gpu_time_ms = 0.0;
    uint32_t metrics_count = 0;
    uint32_t packet_count_since_print = 0;
    std::chrono::steady_clock::time_point last_debug_print;
    bool first_packet = true;

    FrameInput last_input{};
    bool has_valid_input = false;

    // Asynchronous map loading state
    std::future<MapParser::MapMesh> map_load_future;
    bool map_loading = false;
    std::string current_map;

    GLFWwindow* current_context = nullptr;
    void make_context_current(GLFWwindow* target);
};
