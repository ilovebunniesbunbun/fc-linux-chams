#include "App.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <thread>

#include "overlay/esp_drawing.hpp"
#include "logger.hpp"
#include "math/BoneMath.hpp"
#include "math/ViewMatrix.hpp"
#include "renderer/Passes.hpp"

App::App(const OverlayConfig& initial_cfg) : cfg(initial_cfg), last_debug_print(std::chrono::steady_clock::now())
{
}

App::~App()
{
    visibility_worker.stop();
    esp_renderer.cleanup();
    shm.shutdown();
}

void App::make_context_current(GLFWwindow* target)
{
    if (current_context != target) {
        glfwMakeContextCurrent(target);
        current_context = target;
    }
}

bool App::initialize_system()
{
    // Initialize GLFW early to discover monitors
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    if (!glfwInit()) {
        FC2_LOG_ERROR("Failed to initialize GLFW for monitor discovery.");
        return false;
    }

    AgentParser::SetCustomVpkPath(cfg.vpk_path);
    FC2_LOG_INFO(
        "Loaded config: monitor={}x{} game={}x{} scaling={} fps={} show_fps={} monitor_index={} gpu_preference={}",
        cfg.monitor_w, cfg.monitor_h, cfg.game_w, cfg.game_h, cfg.scaling, cfg.fps, cfg.show_fps ? "true" : "false",
        cfg.monitor_index, cfg.gpu_preference);

    int monitor_w = cfg.monitor_w;
    int monitor_h = cfg.monitor_h;
    int offset_x = 0;
    int offset_y = 0;

    int monitor_count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitor_count);
    if (monitors && monitor_count > 0) {
        int target_idx = cfg.monitor_index;
        if (target_idx < 0 || target_idx >= monitor_count) {
            target_idx = 0;
        }
        GLFWmonitor* monitor = monitors[target_idx];
        glfwGetMonitorPos(monitor, &offset_x, &offset_y);
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode) {
            monitor_w = mode->width;
            monitor_h = mode->height;
        }
        FC2_LOG_INFO("Target monitor [{}] ({}) size: {}x{} pos: ({}, {})", target_idx, glfwGetMonitorName(monitor),
                     monitor_w, monitor_h, offset_x, offset_y);
    }

    window_width = monitor_w;
    window_height = monitor_h;
    window_x = offset_x;
    window_y = offset_y;

    if (cfg.scaling == "centered") {
        float monitor_aspect = (float)monitor_w / monitor_h;
        float game_aspect = (float)cfg.game_w / cfg.game_h;
        if (game_aspect < monitor_aspect) {
            window_height = monitor_h;
            window_width = (int)(monitor_h * game_aspect);
            window_x = offset_x + (monitor_w - window_width) / 2;
            window_y = offset_y;
        } else {
            window_width = monitor_w;
            window_height = (int)(monitor_w / game_aspect);
            window_x = offset_x;
            window_y = offset_y + (monitor_h - window_height) / 2;
        }
    } else if (cfg.scaling == "custom") {
        window_width = cfg.game_w;
        window_height = cfg.game_h;
        window_x = offset_x + cfg.game_x;
        window_y = offset_y + cfg.game_y;
    } else {
        // stretched
        window_width = monitor_w;
        window_height = monitor_h;
        window_x = offset_x;
        window_y = offset_y;
    }

    if (window_width == monitor_w && window_height == monitor_h) {
        window_height -= 1;
    }

    FC2_LOG_INFO("Window geometry: {}x{} at ({}, {})", window_width, window_height, window_x, window_y);

    overlay = std::make_unique<OverlayClient>(window_width, window_height, window_x, window_y, cfg.hyprland_support);

    // Initialize GLEW to load modern OpenGL function pointers
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        FC2_LOG_ERROR("Failed to initialize GLEW: {}", reinterpret_cast<const char*>(glewGetErrorString(err)));
        return false;
    }
    FC2_LOG_INFO("Loaded modern OpenGL core functions.");

    menu = std::make_unique<MenuClient>(cfg);
    menu->set_overlay_window(overlay->get_window());

    // Make overlay context current again so shaders and buffers are compiled on the overlay window context
    make_context_current(overlay->get_window());

    // Apply initial VSync setting from config
    glfwSwapInterval(cfg.vsync ? 1 : 0);

    if (!chams_renderer.init()) {
        FC2_LOG_ERROR("Failed to compile and link chams shader programs.");
        return false;
    }

    if (!depth_prepass.init()) {
        FC2_LOG_ERROR("Failed to initialize depth prepass shaders.");
        return false;
    }

    if (!esp_renderer.init()) {
        FC2_LOG_ERROR("Failed to initialize ESP renderer.");
        return false;
    }

    if (!shm.initialize()) {
        FC2_LOG_ERROR("POSIX Shared Memory segment not initialized. Start Lua collector first.");
        return false;
    }

    svg_cache.initialize("assets");

    visibility_worker.start();
    return true;
}

int App::run()
{
    if (!initialize_system()) {
        return 1;
    }

    while (!overlay->should_close() && !menu->should_close()) {
        process_frame();
    }

    return 0;
}

void App::process_frame()
{
    scheduler.begin_frame();
    gpu_profiler.begin_frame(cfg.debug_gpu_profiling);

    // Ensure the overlay context is current for all uploads and rendering
    make_context_current(overlay->get_window());

    overlay->poll_events();

    // Handle config updates dynamically
    if (cfg.is_dirty()) {
        scheduler.update_vsync(cfg.vsync, overlay->get_window());
        cfg.clear_dirty();
    }

    update_map_loading();

    // Drain old semaphore signals to catch up to the freshest game frame
    while (shm.try_frame()) {}

    // Wait up to 50ms for the game to produce a brand new frame
    shm.wait_for_frame(50);

    prepare_frame_input(last_input);

    if (last_input.packet.player_count >= 0 && static_cast<size_t>(last_input.packet.player_count) <= shm::MAX_PLAYERS) {
        FrameState state;
        preprocess_frame_state(last_input, state);
        render_frame(last_input, state);
    } else {
        // Prevent window freeze if game is completely unavailable
        overlay->begin_frame();
        overlay->end_frame();
    }

    render_menu();

    // Frame pacing limiter
    scheduler.pace_frame(cfg.fps);

    gpu_profiler.end_frame();
}

void App::update_map_loading()
{
    if (map_loading && map_load_future.valid() &&
        map_load_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto mesh = map_load_future.get();
        map_loading = false;
        if (mesh.Valid && !mesh.Triangles.empty()) {
            depth_prepass.upload_geometry(mesh.VisualTriangles);

            // Signal worker thread to reload map geometry for BVH checks
            visibility_worker.update_map(mesh, cfg);

            FC2_LOG_INFO("Async map geometry loaded ({} triangles).", mesh.Triangles.size());
        } else {
            FC2_LOG_INFO("Async map geometry unavailable, clearing GPU prepass.");
            depth_prepass.clear_geometry();
        }
    }
}

void App::prepare_frame_input(FrameInput& input)
{
    auto ipc_start = std::chrono::high_resolution_clock::now();
    bool has_new_packet = shm.fetch_latest(input.packet);
    auto ipc_end = std::chrono::high_resolution_clock::now();
    double current_ipc_ms = std::chrono::duration<double, std::milli>(ipc_end - ipc_start).count();

    if (has_new_packet) {
        packet_count_since_print++;
        visibility_worker.submit_work(input.packet, cfg);

        if (first_packet) {
            FC2_LOG_INFO("Shared memory bridge communication active.");
            first_packet = false;
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_debug_print).count() >= 5) {
            double avg_ipc = metrics_count > 0 ? total_ipc_time_ms / metrics_count : 0.0;
            double avg_cpu = metrics_count > 0 ? total_cpu_time_ms / metrics_count : 0.0;
            double avg_gpu = metrics_count > 0 ? total_gpu_time_ms / metrics_count : 0.0;
            double avg_total = avg_ipc + avg_cpu + avg_gpu;

            if (cfg.debug_bridge && !cfg.debug_gpu_profiling) {
                FC2_LOG_INFO("Bridge active - received {} packets in last 5s (frame index: {}, players tracked: {})",
                             packet_count_since_print, input.packet.frame_index, input.packet.player_count);
                FC2_LOG_INFO("            Avg Timeline (frame processing latency):");
                FC2_LOG_INFO("            - IPC Read Time:   {:.3f} ms", avg_ipc);
                FC2_LOG_INFO("            - CPU Math Time:   {:.3f} ms", avg_cpu);
                FC2_LOG_INFO("            - GPU Render Time: {:.3f} ms", avg_gpu);
                FC2_LOG_INFO("            - Total Pipeline:  {:.3f} ms", avg_total);
            } else if (cfg.debug_gpu_profiling) {
                gpu_profiler.print_and_reset(packet_count_since_print, input.packet.frame_index, input.packet.player_count, avg_ipc, avg_cpu, avg_total);
            }
            last_debug_print = now;
            packet_count_since_print = 0;
            total_ipc_time_ms = 0.0;
            total_cpu_time_ms = 0.0;
            total_gpu_time_ms = 0.0;
            metrics_count = 0;
        }

        // Map change handling
        std::string shm_map = input.packet.map_name;
        if (current_map != shm_map) {
            current_map = shm_map;
            if (!current_map.empty()) {
                FC2_LOG_INFO("Map change detected: {}", current_map);

                // Clear previous map geometries immediately
                depth_prepass.clear_geometry();

                map_loading = true;
                std::string map_name_copy = current_map;
                map_load_future =
                    std::async(std::launch::async, [map_name_copy]() { return MapParser::LoadMesh(map_name_copy); });
            } else {
                FC2_LOG_INFO("Left map, clearing map geometries.");
                depth_prepass.clear_geometry();
                map_loading = false;
            }
        }
    }

    // View matrix resolution (optional extrapolation)
    float latest_view_matrix[16];
    bool has_matrix = shm.read_view_matrix(latest_view_matrix);
    if (cfg.extrapolate) {
        static ViewMatrixExtrapolator extrapolator;
        if (has_matrix) {
            extrapolator.update(latest_view_matrix);
        } else {
            extrapolator.update(input.packet.view_matrix);
        }
        extrapolator.get_extrapolated_matrix(input.render_view_matrix);
    } else {
        if (has_matrix) {
            std::memcpy(input.render_view_matrix, latest_view_matrix, sizeof(float) * 16);
        } else {
            std::memcpy(input.render_view_matrix, input.packet.view_matrix, sizeof(float) * 16);
        }
    }

    input.dt = scheduler.get_delta_time();

    // Transpose row-major View-Proj to column-major for OpenGL shaders
    for (int c = 0; c < 4; c++) {
        input.gl_vp[c * 4 + 0] = input.render_view_matrix[0 * 4 + c];
        input.gl_vp[c * 4 + 1] = input.render_view_matrix[1 * 4 + c];
        input.gl_vp[c * 4 + 2] = input.render_view_matrix[2 * 4 + c];
        input.gl_vp[c * 4 + 3] = input.render_view_matrix[3 * 4 + c];
    }

    input.cam_pos = get_camera_position(input.render_view_matrix);
    input.cam_pos_arr[0] = input.cam_pos.x;
    input.cam_pos_arr[1] = input.cam_pos.y;
    input.cam_pos_arr[2] = input.cam_pos.z;

    // Detailed debug logging throttled to 5 seconds
    static auto last_detail_print = std::chrono::steady_clock::now();
    auto now_detail = std::chrono::steady_clock::now();
    if (cfg.debug_bridge &&
        std::chrono::duration_cast<std::chrono::seconds>(now_detail - last_detail_print).count() >= 5) {
        last_detail_print = now_detail;
        FC2_LOG_DEBUG("Window Size: {}x{}", window_width, window_height);
        FC2_LOG_DEBUG("Camera Pos: ({:.3f}, {:.3f}, {:.3f})", input.cam_pos.x, input.cam_pos.y, input.cam_pos.z);

        std::stringstream ss;
        for (int i = 0; i < 16; ++i) ss << input.render_view_matrix[i] << " ";
        FC2_LOG_DEBUG("ViewProj Matrix: {}", ss.str());

        FC2_LOG_DEBUG("Players Tracked: {}", input.packet.player_count);
        for (int i = 0; i < std::min(input.packet.player_count, 3); ++i) {
            const auto& p = input.packet.players[i];
            FC2_LOG_DEBUG("Player {}: active={} team={} hp={} name={} origin=({:.3f}, {:.3f}, {:.3f}) bone_count={}", i,
                          p.active, p.team, p.health, p.model_name, p.origin.x, p.origin.y, p.origin.z, p.bone_count);

            if (p.active) {
                if (p.bone_count > 0) {
                    FC2_LOG_DEBUG("            Bone 0 Pos: ({:.3f}, {:.3f}, {:.3f})", p.bones[0].position.x,
                                  p.bones[0].position.y, p.bones[0].position.z);
                }
                if (p.bone_count > 7) {
                    FC2_LOG_DEBUG("            Bone 7 Pos: ({:.3f}, {:.3f}, {:.3f})", p.bones[7].position.x,
                                  p.bones[7].position.y, p.bones[7].position.z);
                }

                float sx = 0.0f, sy = 0.0f;
                bool screen_ok =
                    world_to_screen(p.origin, &sx, &sy, input.render_view_matrix, window_width, window_height);
                FC2_LOG_DEBUG("            WorldToScreen(Origin): ok={} screen=({:.3f}, {:.3f})",
                              screen_ok ? "true" : "false", sx, sy);
            }
        }
    }

    // Accumulate metric part 1 (only if we actually fetched a new packet)
    if (has_new_packet) {
        total_ipc_time_ms += current_ipc_ms;
    }
}

void App::preprocess_frame_state(const FrameInput& input, FrameState& state)
{
    auto cpu_start = std::chrono::high_resolution_clock::now();

    // Fetch latest results from worker thread
    visibility_worker.get_latest_result(state.vischeck_result);

    // Persistent static scratch buffers to avoid per-player heap allocations
    static std::vector<source2::Mat3x4> world_bones_buf;
    static std::vector<source2::Mat3x4> skinning_palette_buf;
    static std::vector<glm::vec3> sanitized_bones_buf;
    static std::vector<float> batch_bones;

    // Only grow dummy_vis, never shrink; fill with 1.0f
    size_t needed_vis = static_cast<size_t>(input.packet.player_count) * shm::MAX_BONES;
    if (state.dummy_vis.size() < needed_vis) {
        state.dummy_vis.assign(needed_vis, 1.0f);
    }

    state.render_palettes.resize(input.packet.player_count);
    for (int i = 0; i < input.packet.player_count; ++i) {
        const auto& player = input.packet.players[i];
        if (!player.active) {
            state.render_palettes[i].model = nullptr;
            continue;
        }

        std::string model_key = player.model_name;
        if (player.has_defuser) {
            model_key += "#defuser";
        }

        const auto* model = model_cache.get_or_load(model_key);
        if (!model || !model->valid) {
            state.render_palettes[i].model = nullptr;
            continue;
        }

        // Convert raw bone transforms to 3x4 world matrices (reuse static buffer)
        const int bc = model->mesh.bone_count;
        world_bones_buf.resize(bc);
        for (int j = 0; j < bc; ++j) {
            if (j < player.bone_count) {
                world_bones_buf[j] = matrix_from_bone(player.bones[j].position, player.bones[j].rotation);
            } else {
                source2::Mat3x4 identity{};
                identity[0][0] = identity[1][1] = identity[2][2] = 1.0f;
                world_bones_buf[j] = identity;
            }
        }

        // Sanitise bones for LOD issues
        sanitize_lod_bones(world_bones_buf.data(), bc, model->mesh);

        // Extract sanitized world positions for ESP logic (reuse static buffer)
        sanitized_bones_buf.resize(bc);
        for (int j = 0; j < bc; ++j) {
            sanitized_bones_buf[j] = {world_bones_buf[j][0][3], world_bones_buf[j][1][3], world_bones_buf[j][2][3]};
        }

        // Multiply by inverse bind poses to build final skinned joint palette (reuse static buffer)
        skinning_palette_buf.resize(bc);
        for (int j = 0; j < bc; ++j) {
            if (j < static_cast<int>(model->mesh.inv_bind_poses.size())) {
                skinning_palette_buf[j] = multiply_3x4(world_bones_buf[j], model->mesh.inv_bind_poses[j]);
            } else {
                skinning_palette_buf[j] = world_bones_buf[j];
            }
        }

        bool is_player_visible = true;

        state.render_palettes[i] = {model, skinning_palette_buf, is_player_visible, sanitized_bones_buf};
    }

    // Batch convert and upload bone matrices to UBO (write directly into static buffer)
    batch_bones.clear();
    batch_bones.resize(static_cast<size_t>(input.packet.player_count) * shm::MAX_BONES * 16, 0.0f);
    state.player_ubo_slots.assign(input.packet.player_count, -1);
    int current_slot = 0;

    for (int i = 0; i < input.packet.player_count; ++i) {
        const auto& rp = state.render_palettes[i];
        if (!rp.model) continue;

        float* slot_base = batch_bones.data() + static_cast<size_t>(current_slot) * shm::MAX_BONES * 16;

        for (size_t j = 0; j < rp.skinning_palette.size() && j < shm::MAX_BONES; ++j) {
            const auto& b = rp.skinning_palette[j];
            float* dest = slot_base + j * 16;
            // Column 0
            dest[0] = b[0][0];
            dest[1] = b[0][1];
            dest[2] = b[0][2];
            dest[3] = 0.0f;
            // Column 1
            dest[4] = b[1][0];
            dest[5] = b[1][1];
            dest[6] = b[1][2];
            dest[7] = 0.0f;
            // Column 2
            dest[8] = b[2][0];
            dest[9] = b[2][1];
            dest[10] = b[2][2];
            dest[11] = 0.0f;
            // Column 3
            dest[12] = b[0][3];
            dest[13] = b[1][3];
            dest[14] = b[2][3];
            dest[15] = 1.0f;
        }
        for (size_t j = rp.skinning_palette.size(); j < shm::MAX_BONES; ++j) {
            float* dest = slot_base + j * 16;
            dest[0] = dest[5] = dest[10] = dest[15] = 1.0f;
        }

        state.player_ubo_slots[i] = current_slot++;
    }

    if (current_slot > 0) {
        chams_renderer.upload_bones_batch(batch_bones.data(), current_slot);
    }

    // Sort active player indices by model VAO to optimize state changes
    state.sorted_player_indices.clear();
    state.sorted_player_indices.reserve(input.packet.player_count);
    for (int i = 0; i < input.packet.player_count; ++i) {
        if (state.render_palettes[i].model) {
            state.sorted_player_indices.push_back(i);
        }
    }
    std::sort(state.sorted_player_indices.begin(), state.sorted_player_indices.end(),
              [&](int a, int b) { return state.render_palettes[a].model->vao < state.render_palettes[b].model->vao; });

    auto cpu_end = std::chrono::high_resolution_clock::now();
    total_cpu_time_ms += std::chrono::duration<double, std::milli>(cpu_end - cpu_start).count();
}
void App::render_frame(const FrameInput& input, FrameState& state)
{
    auto gpu_start = std::chrono::high_resolution_clock::now();

    // Update grenade tracker simulation
    grenade_tracker.update(state.vischeck_result, glfwGetTime(), input.dt, cfg);

    overlay->begin_frame();

    // Orchestrate 3D player rendering passes
    ChamsPassInputs passes_inputs{input,         state,        cfg,          chams_renderer,
                                  depth_prepass, esp_renderer, &gpu_profiler, window_width, window_height};
    ChamsPass::run(passes_inputs);

    // 5. Render 3D Map Collision Wireframe Visualizer
    if (cfg.map_visualizer_enabled) {
        depth_prepass.render_wireframe(input.gl_vp, cfg.map_visualizer_color, cfg.map_visualizer_depth_tested);
    }

    // 6. Draw grenade trajectories
    grenade_renderer.render(grenade_tracker, esp_renderer, state.vischeck_result, input.gl_vp, input.render_view_matrix, cfg, overlay.get(), svg_cache);

    gpu_profiler.begin_section(GpuTimerSection::ESP_2D);

    // 7. Render 2D ESP Overlay (Skeleton, Box, Health Bar)
    if (cfg.esp_enabled) {
        static float smoothed_health[shm::MAX_PLAYERS];
        static bool player_active_last[shm::MAX_PLAYERS] = {false};
        static bool first_init = true;
        if (first_init) {
            std::fill(std::begin(smoothed_health), std::end(smoothed_health), -1.0f);
            first_init = false;
        }

        if (cfg.esp_box || cfg.esp_health_bar) {
            glDisable(GL_DEPTH_TEST);
            esp_renderer.clear();
            esp_renderer.set_ortho(0, window_width, window_height, 0);

            for (int i = 0; i < input.packet.player_count; ++i) {
                const auto& rp = state.render_palettes[i];
                if (!rp.model) {
                    player_active_last[i] = false;
                    continue;
                }
                const auto& player = input.packet.players[i];

                if (!player_active_last[i] || smoothed_health[i] < 0.0f) {
                    smoothed_health[i] = static_cast<float>(player.health);
                    player_active_last[i] = true;
                } else {
                    smoothed_health[i] +=
                        (player.health - smoothed_health[i]) * std::clamp(10.0f * input.dt, 0.0f, 1.0f);
                    if (std::abs(player.health - smoothed_health[i]) > 50.0f) {
                        smoothed_health[i] = static_cast<float>(player.health);
                    }
                }

                float bx, by, bw, bh;
                if (get_player_bounds(rp.sanitized_bones, player.origin, input.packet.local_eye,
                                      input.render_view_matrix, window_width, window_height, bx, by, bw, bh, cfg)) {
                    if (cfg.esp_box) {
                        esp_renderer.add_outlined_rect_2d(bx, by, bw, bh, cfg.esp_box_color, cfg.esp_box_thickness,
                                                          cfg.esp_box_outline);
                    }
                    if (cfg.esp_health_bar) {
                        esp_renderer.add_health_bar_2d(bx, by, bw, bh, smoothed_health[i], cfg);
                    }
                }
            }

            esp_renderer.flush_triangles();
            esp_renderer.flush_lines();
            glEnable(GL_DEPTH_TEST);
        }
    }

    // 8. Draw 2D FPS Counter Overlay
    if (cfg.show_fps) {
        static int frame_count = 0;
        static double last_fps_time = 0.0;
        static int current_fps = 0;

        double current_time = glfwGetTime();
        frame_count++;
        if (current_time - last_fps_time >= 1.0) {
            current_fps = frame_count;
            frame_count = 0;
            last_fps_time = current_time;
        }
        overlay->draw_fps(current_fps);
    }

    gpu_profiler.end_section(GpuTimerSection::ESP_2D);

    overlay->end_frame();

    auto gpu_end = std::chrono::high_resolution_clock::now();
    total_gpu_time_ms += std::chrono::duration<double, std::milli>(gpu_end - gpu_start).count();
    metrics_count++;
}

void App::render_menu()
{
    static auto last_menu_render = std::chrono::steady_clock::now();
    auto now_menu = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now_menu - last_menu_render).count() >= 16) {
        menu->render();
        current_context = menu->get_window();
        last_menu_render = now_menu;
    }
}
