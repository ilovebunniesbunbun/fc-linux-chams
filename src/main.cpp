#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <future>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <cmath>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

#include "shm_reader.hpp"
#include "overlay_client.hpp"
#include "renderer/gl_loader.hpp"
#include "renderer/gpu_chams.hpp"
#include "renderer/depth_prepass.hpp"
#include "model_cache.hpp"
#include "vpk/vmdl/model.hpp"
#include "vpk/vmdl/maps/map_parser.hpp"
#include "config.hpp"
#include "menu_client.hpp"

using json = nlohmann::json;



class ViewMatrixExtrapolator {
private:
    float prev_matrix[16]{};
    float curr_matrix[16]{};
    std::chrono::high_resolution_clock::time_point prev_time;
    std::chrono::high_resolution_clock::time_point curr_time;
    bool has_prev = false;
    bool has_curr = false;

    bool matrix_equals(const float* a, const float* b) {
        for (int i = 0; i < 16; ++i) {
            if (std::abs(a[i] - b[i]) > 1e-7f) return false;
        }
        return true;
    }

public:
    void update(const float* new_matrix) {
        auto now = std::chrono::high_resolution_clock::now();
        if (!has_curr) {
            std::memcpy(curr_matrix, new_matrix, sizeof(float) * 16);
            curr_time = now;
            has_curr = true;
            return;
        }

        if (matrix_equals(curr_matrix, new_matrix)) {
            return;
        }

        std::memcpy(prev_matrix, curr_matrix, sizeof(float) * 16);
        prev_time = curr_time;
        has_prev = true;

        std::memcpy(curr_matrix, new_matrix, sizeof(float) * 16);
        curr_time = now;
    }

    void get_extrapolated_matrix(float* out_matrix) {
        auto now = std::chrono::high_resolution_clock::now();
        if (!has_curr) {
            std::memset(out_matrix, 0, sizeof(float) * 16);
            return;
        }
        if (!has_prev) {
            std::memcpy(out_matrix, curr_matrix, sizeof(float) * 16);
            return;
        }

        auto dt_update = std::chrono::duration<double, std::milli>(curr_time - prev_time).count();
        auto dt_render = std::chrono::duration<double, std::milli>(now - curr_time).count();

        if (dt_update < 1.0 || dt_update > 200.0) {
            std::memcpy(out_matrix, curr_matrix, sizeof(float) * 16);
            return;
        }

        double t = dt_render / dt_update;
        if (t > 2.0) t = 2.0;
        if (t < 0.0) t = 0.0;

        for (int i = 0; i < 16; ++i) {
            out_matrix[i] = curr_matrix[i] + static_cast<float>(t) * (curr_matrix[i] - prev_matrix[i]);
        }
    }
};

bool invert_matrix(const float* m, float* out) {
    float inv[16];
    inv[0] = m[5]  * m[10] * m[15] - 
             m[5]  * m[11] * m[14] - 
             m[9]  * m[6]  * m[15] + 
             m[9]  * m[7]  * m[14] +
             m[13] * m[6]  * m[11] - 
             m[13] * m[7]  * m[10];

    inv[4] = -m[4]  * m[10] * m[15] + 
              m[4]  * m[11] * m[14] + 
              m[8]  * m[6]  * m[15] - 
              m[8]  * m[7]  * m[14] - 
              m[12] * m[6]  * m[11] + 
              m[12] * m[7]  * m[10];

    inv[8] = m[4]  * m[9]  * m[15] - 
             m[4]  * m[11] * m[13] - 
             m[8]  * m[5]  * m[15] + 
             m[8]  * m[7]  * m[13] + 
             m[12] * m[5]  * m[11] - 
             m[12] * m[7]  * m[9];

    inv[12] = -m[4]  * m[9]  * m[14] + 
               m[4]  * m[10] * m[13] + 
               m[8]  * m[5]  * m[14] - 
               m[8]  * m[6]  * m[13] - 
               m[12] * m[5]  * m[10] + 
               m[12] * m[6]  * m[9];

    inv[1] = -m[1]  * m[10] * m[15] + 
              m[1]  * m[11] * m[14] + 
              m[9]  * m[2]  * m[15] - 
              m[9]  * m[3]  * m[14] - 
              m[13] * m[2]  * m[11] + 
              m[13] * m[3]  * m[10];

    inv[5] = m[0]  * m[10] * m[15] - 
             m[0]  * m[11] * m[14] - 
             m[8]  * m[2]  * m[15] + 
             m[8]  * m[3]  * m[14] + 
             m[12] * m[2]  * m[11] - 
             m[12] * m[3]  * m[10];

    inv[9] = -m[0]  * m[9]  * m[15] + 
              m[0]  * m[11] * m[13] + 
              m[8]  * m[1]  * m[15] - 
              m[8]  * m[3]  * m[13] - 
              m[12] * m[1]  * m[11] + 
              m[12] * m[3]  * m[9];

    inv[13] = m[0]  * m[9]  * m[14] - 
              m[0]  * m[10] * m[13] - 
              m[8]  * m[1]  * m[14] + 
              m[8]  * m[2]  * m[13] + 
              std::uintptr_t(12) * m[1]  * m[10] - 
              std::uintptr_t(12) * m[2]  * m[9]; // Fixed placeholder calculation to match original inversion
    
    // Explicit inversion elements calculated correctly
    inv[13] = m[0]  * m[9]  * m[14] - 
              m[0]  * m[10] * m[13] - 
              m[8]  * m[1]  * m[14] + 
              m[8]  * m[2]  * m[13] + 
              m[12] * m[1]  * m[10] - 
              m[12] * m[2]  * m[9];

    inv[2] = m[1]  * m[6]  * m[15] - 
             m[1]  * m[7]  * m[14] - 
             m[5]  * m[2]  * m[15] + 
             m[5]  * m[3]  * m[14] + 
             m[13] * m[2]  * m[7] - 
             m[13] * m[3]  * m[6];

    inv[6] = -m[0]  * m[6]  * m[15] + 
              m[0]  * m[7]  * m[14] + 
              m[5]  * m[2]  * m[15] - 
              m[5]  * m[3]  * m[14] - 
              m[12] * m[2]  * m[7] + 
              m[12] * m[3]  * m[6];

    inv[10] = m[0]  * m[5]  * m[15] - 
              m[0]  * m[7]  * m[13] - 
              m[5]  * m[1]  * m[15] + 
              m[5]  * m[3]  * m[13] + 
              m[12] * m[1]  * m[7] - 
              m[12] * m[3]  * m[5];

    inv[14] = -m[0]  * m[5]  * m[14] + 
               m[0]  * m[6]  * m[13] + 
               m[5]  * m[1]  * m[14] - 
               m[5]  * m[2]  * m[13] - 
               m[12] * m[1]  * m[6] + 
               m[12] * m[2]  * m[5];

    inv[3] = -m[1]  * m[6]  * m[11] + 
              m[1]  * m[7]  * m[10] + 
              m[5]  * m[2]  * m[11] - 
              m[5]  * m[3]  * m[10] - 
              m[9]  * m[2]  * m[7] + 
              m[9]  * m[3]  * m[6];

    inv[7] = m[0]  * m[6]  * m[11] - 
             m[0]  * m[7]  * m[10] - 
             m[5]  * m[2]  * m[11] + 
             m[5]  * m[3]  * m[10] + 
             m[8]  * m[2]  * m[7] - 
             m[8]  * m[3]  * m[6];

    inv[11] = -m[0]  * m[5]  * m[11] + 
               m[0]  * m[7]  * m[9] + 
               m[5]  * m[1]  * m[11] - 
               m[5]  * m[3]  * m[9] - 
               m[8]  * m[1]  * m[7] + 
               m[8]  * m[3]  * m[5];

    inv[15] = m[0]  * m[5]  * m[10] - 
              m[0]  * m[6]  * m[9] - 
              m[5]  * m[1]  * m[10] + 
              m[5]  * m[2]  * m[9] + 
              m[8]  * m[1]  * m[6] - 
              m[8]  * m[2]  * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0) return false;

    det = 1.0f / det;
    for (int i = 0; i < 16; i++) {
        out[i] = inv[i] * det;
    }
    return true;
}

Vec3 get_camera_position(const float* view_proj) {
    float inv[16];
    if (!invert_matrix(view_proj, inv)) {
        return {0, 0, 0};
    }
    Vec3 cam;
    float w = inv[11] * -1.0f + inv[15];
    cam.x = (inv[8] * -1.0f + inv[12]) / w;
    cam.y = (inv[9] * -1.0f + inv[13]) / w;
    cam.z = (inv[10] * -1.0f + inv[14]) / w;
    return cam;
}

inline source2::Mat3x4 matrix_from_bone(const ::Vec3& pos, const ::Vec4& rot) {
    const auto qx = rot.x;
    const auto qy = rot.y;
    const auto qz = rot.z;
    const auto qw = rot.w;

    const auto xx = qx * qx;
    const auto yy = qy * qy;
    const auto zz = qz * qz;
    const auto xy = qx * qy;
    const auto xz = qx * qz;
    const auto yz = qy * qz;
    const auto wx = qw * qx;
    const auto wy = qw * qy;
    const auto wz = qw * qz;

    source2::Mat3x4 out{};
    out.mat[0][0] = 1.0f - 2.0f * (yy + zz);
    out.mat[1][0] = 2.0f * (xy + wz);
    out.mat[2][0] = 2.0f * (xz - wy);
    out.mat[0][1] = 2.0f * (xy - wz);
    out.mat[1][1] = 1.0f - 2.0f * (xx + zz);
    out.mat[2][1] = 2.0f * (yz + wx);
    out.mat[0][2] = 2.0f * (xz + wy);
    out.mat[1][2] = 2.0f * (yz - wx);
    out.mat[2][2] = 1.0f - 2.0f * (xx + yy);
    out.mat[0][3] = pos.x;
    out.mat[1][3] = pos.y;
    out.mat[2][3] = pos.z;
    return out;
}

inline source2::Mat3x4 multiply_3x4(const source2::Mat3x4& a, const source2::Mat3x4& b) {
    source2::Mat3x4 out{};
    for (int r = 0; r < 3; ++r) {
        out.mat[r][0] = a.mat[r][0]*b.mat[0][0] + a.mat[r][1]*b.mat[1][0] + a.mat[r][2]*b.mat[2][0];
        out.mat[r][1] = a.mat[r][0]*b.mat[0][1] + a.mat[r][1]*b.mat[1][1] + a.mat[r][2]*b.mat[2][1];
        out.mat[r][2] = a.mat[r][0]*b.mat[0][2] + a.mat[r][1]*b.mat[1][2] + a.mat[r][2]*b.mat[2][2];
        out.mat[r][3] = a.mat[r][0]*b.mat[0][3] + a.mat[r][1]*b.mat[1][3] + a.mat[r][2]*b.mat[2][3] + a.mat[r][3];
    }
    return out;
}

void sanitize_lod_bones(source2::Mat3x4* bones, int bone_count, const AgentParser::AgentMesh& mesh) {
    const int num_ibm = static_cast<int>(mesh.inv_bind_poses.size());
    const int num_parents = static_cast<int>(mesh.bone_parents.size());
    if (num_parents == 0 || num_ibm == 0) return;

    const int limit = (std::min)({ bone_count, num_ibm, num_parents });
    if (limit <= 28) return;

    const float org_x = bones[0].mat[0][3];
    const float org_y = bones[0].mat[1][3];
    const float org_z = bones[0].mat[2][3];
    const float org_dist_sq = org_x * org_x + org_y * org_y + org_z * org_z;

    auto bind_pos = [&](int idx, float& bx, float& by, float& bz) {
        const auto& m = mesh.inv_bind_poses[idx];
        const float tx = m.mat[0][3], ty = m.mat[1][3], tz = m.mat[2][3];
        bx = -(m.mat[0][0] * tx + m.mat[1][0] * ty + m.mat[2][0] * tz);
        by = -(m.mat[0][1] * tx + m.mat[1][1] * ty + m.mat[2][1] * tz);
        bz = -(m.mat[0][2] * tx + m.mat[1][2] * ty + m.mat[2][2] * tz);
    };

    std::vector<uint8_t> exploded(limit, 0);

    for (int i = 28; i < limit; ++i) {
        const int16_t p = mesh.bone_parents[i];
        if (p < 0 || p == i || p >= limit) continue;

        float bi_x, bi_y, bi_z, bp_x, bp_y, bp_z;
        bind_pos(i, bi_x, bi_y, bi_z);
        bind_pos(p, bp_x, bp_y, bp_z);
        const float dd_x = bi_x - bp_x, dd_y = bi_y - bp_y, dd_z = bi_z - bp_z;

        const auto& ibm_p = mesh.inv_bind_poses[p];
        const float lx = ibm_p.mat[0][0] * dd_x + ibm_p.mat[0][1] * dd_y + ibm_p.mat[0][2] * dd_z;
        const float ly = ibm_p.mat[1][0] * dd_x + ibm_p.mat[1][1] * dd_y + ibm_p.mat[1][2] * dd_z;
        const float lz = ibm_p.mat[2][0] * dd_x + ibm_p.mat[2][1] * dd_y + ibm_p.mat[2][2] * dd_z;

        const auto& gp = bones[p];
        const float wx = gp.mat[0][0] * lx + gp.mat[0][1] * ly + gp.mat[0][2] * lz;
        const float wy = gp.mat[1][0] * lx + gp.mat[1][1] * ly + gp.mat[1][2] * lz;
        const float wz = gp.mat[2][0] * lx + gp.mat[2][1] * ly + gp.mat[2][2] * lz;

        const float recon_x = gp.mat[0][3] + wx;
        const float recon_y = gp.mat[1][3] + wy;
        const float recon_z = gp.mat[2][3] + wz;

        bool garbage = false;

        if (exploded[p]) {
            garbage = true;
        }

        if (!garbage) {
            const float dx = bones[i].mat[0][3] - org_x;
            const float dy = bones[i].mat[1][3] - org_y;
            const float dz = bones[i].mat[2][3] - org_z;
            if (dx * dx + dy * dy + dz * dz > 120.0f * 120.0f)
                garbage = true;
        }

        if (!garbage) {
            const float dx = bones[i].mat[0][3] - recon_x;
            const float dy = bones[i].mat[1][3] - recon_y;
            const float dz = bones[i].mat[2][3] - recon_z;
            if (dx * dx + dy * dy + dz * dz > 60.0f * 60.0f)
                garbage = true;
        }

        if (!garbage) {
            const float bx = bones[i].mat[0][3], by = bones[i].mat[1][3], bz = bones[i].mat[2][3];
            if (org_dist_sq > 500.0f * 500.0f && (bx * bx + by * by + bz * bz) < 50.0f * 50.0f)
                garbage = true;
        }

        if (garbage) {
            bones[i] = bones[p];
            bones[i].mat[0][3] = recon_x;
            bones[i].mat[1][3] = recon_y;
            bones[i].mat[2][3] = recon_z;
            exploded[i] = 1;
        }
    }
}

struct Vec2 {
    float x, y;
};

#include "esp_drawing.hpp"
#include "renderer/esp_renderer.hpp"

int main() {
    std::cout << "FC2 CHAMS V2: Launching GPU-Skinned Linux Overlay..." << std::endl;

    OverlayConfig cfg = load_config("overlay.json");
    AgentParser::SetCustomVpkPath(cfg.vpk_path);
    std::cout << "FC2 CHAMS V2: Loaded config: monitor=" << cfg.monitor_w << "x" << cfg.monitor_h
              << " game=" << cfg.game_w << "x" << cfg.game_h
              << " scaling=" << cfg.scaling 
              << " fps=" << cfg.fps 
              << " show_fps=" << (cfg.show_fps ? "true" : "false") << std::endl;

    int width = cfg.monitor_w;
    int height = cfg.monitor_h;
    int x = 0;
    int y = 0;

    if (cfg.scaling == "centered") {
        float monitor_aspect = (float)cfg.monitor_w / cfg.monitor_h;
        float game_aspect = (float)cfg.game_w / cfg.game_h;
        if (game_aspect < monitor_aspect) {
            height = cfg.monitor_h;
            width = (int)(cfg.monitor_h * game_aspect);
            x = (cfg.monitor_w - width) / 2;
            y = 0;
        } else {
            width = cfg.monitor_w;
            height = (int)(cfg.monitor_w / game_aspect);
            x = 0;
            y = (cfg.monitor_h - height) / 2;
        }
    } else if (cfg.scaling == "custom") {
        width = cfg.game_w;
        height = cfg.game_h;
        x = cfg.game_x;
        y = cfg.game_y;
    } else {
        // stretched
        width = cfg.monitor_w;
        height = cfg.monitor_h;
        x = 0;
        y = 0;
    }

    if (width == cfg.monitor_w && height == cfg.monitor_h) {
        height -= 1;
    }

    std::cout << "FC2 CHAMS V2: Window geometry: " << width << "x" << height 
              << " at (" << x << ", " << y << ")" << std::endl;

    OverlayClient overlay(width, height, x, y, cfg.hyprland_support);
    std::cout << "FC2 CHAMS V2: Loaded modern OpenGL core functions." << std::endl;

    MenuClient menu(cfg);
    menu.set_overlay_window(overlay.get_window());

    // Make overlay context current again so shaders and buffers are compiled on the overlay window context
    glfwMakeContextCurrent(overlay.get_window());

    // Apply initial VSync setting from config
    glfwSwapInterval(cfg.vsync ? 1 : 0);

    GpuChamsRenderer chams_renderer;
    if (!chams_renderer.init()) {
        std::cerr << "FC2 CHAMS V2: Failed to compile and link chams shader programs." << std::endl;
        return 1;
    }

    DepthPrepassRenderer depth_prepass;
    if (!depth_prepass.init()) {
        std::cerr << "FC2 CHAMS V2: Failed to initialize depth prepass shaders." << std::endl;
        return 1;
    }

    EspRenderer esp_renderer;
    if (!esp_renderer.init()) {
        std::cerr << "FC2 CHAMS V2: Failed to initialize ESP renderer." << std::endl;
        return 1;
    }

    ModelCache model_cache;
    ShmReader shm;

    if (!shm.initialize()) {
        std::cerr << "FC2 CHAMS V2: POSIX Shared Memory segment not initialized. Start Lua collector first." << std::endl;
        return 1;
    }

    ShmPacket packet;
    std::string current_map = "";

    struct AsyncMapResult {
        MapParser::MapMesh mesh;
    };
    std::future<AsyncMapResult> map_load_future;
    bool map_loading = false;

    bool first_packet = true;
    auto last_debug_print = std::chrono::steady_clock::now();
    uint32_t packet_count_since_print = 0;

    double total_ipc_time_ms = 0.0;
    double total_cpu_time_ms = 0.0;
    double total_gpu_time_ms = 0.0;
    uint32_t metrics_count = 0;

    // Track current GL context to skip redundant context switches
    GLFWwindow* current_context = nullptr;
    auto make_context_current = [&current_context](GLFWwindow* target) {
        if (current_context != target) {
            glfwMakeContextCurrent(target);
            current_context = target;
        }
    };

    while (!overlay.should_close() && !menu.should_close()) {
        auto frame_start = std::chrono::high_resolution_clock::now();

        // Ensure the overlay context is current for all uploads and rendering
        make_context_current(overlay.get_window());

        overlay.poll_events();

        // Check if asynchronous map loading has finished
        if (map_loading && map_load_future.valid() && map_load_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = map_load_future.get();
            map_loading = false;
            if (result.mesh.Valid && !result.mesh.Triangles.empty()) {
                depth_prepass.upload_geometry(result.mesh.Triangles);
                std::cout << "FC2 CHAMS V2: Async map geometry loaded (" << result.mesh.Triangles.size() << " triangles)." << std::endl;
            } else {
                std::cout << "FC2 CHAMS V2: Async map geometry unavailable, clearing GPU prepass." << std::endl;
                depth_prepass.clear_geometry();
            }
        }

        shm.try_frame();

        // Dynamically resolve styles in real-time
        int style_vis_id = get_style_id(cfg.style_vis);
        int style_invis_id = get_style_id(cfg.style_invis);

        auto ipc_start = std::chrono::high_resolution_clock::now();
        bool has_new_packet = shm.fetch_latest(packet);
        auto ipc_end = std::chrono::high_resolution_clock::now();
        double current_ipc_ms = std::chrono::duration<double, std::milli>(ipc_end - ipc_start).count();

        if (!has_new_packet) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }

        if (has_new_packet) {
            packet_count_since_print++;
            if (first_packet) {
                std::cout << "FC2 CHAMS V2: Shared memory bridge communication active." << std::endl;
                first_packet = false;
            }

            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_debug_print).count() >= 5) {
                if (cfg.debug_bridge) {
                    double avg_ipc = metrics_count > 0 ? total_ipc_time_ms / metrics_count : 0.0;
                    double avg_cpu = metrics_count > 0 ? total_cpu_time_ms / metrics_count : 0.0;
                    double avg_gpu = metrics_count > 0 ? total_gpu_time_ms / metrics_count : 0.0;
                    double avg_total = avg_ipc + avg_cpu + avg_gpu;

                    std::cout << "FC2 CHAMS V2: Bridge active - received " << packet_count_since_print 
                              << " packets in last 5s (frame index: " << packet.frame_index 
                              << ", players tracked: " << packet.player_count << ")" << std::endl;
                    std::cout << "            Avg Timeline (frame processing latency):" << std::endl;
                    std::cout << "            - IPC Read Time:   " << avg_ipc << " ms" << std::endl;
                    std::cout << "            - CPU Math Time:   " << avg_cpu << " ms" << std::endl;
                    std::cout << "            - GPU Render Time: " << avg_gpu << " ms" << std::endl;
                    std::cout << "            - Total Pipeline:  " << avg_total << " ms" << std::endl;
                }
                last_debug_print = now;
                packet_count_since_print = 0;
                total_ipc_time_ms = 0.0;
                total_cpu_time_ms = 0.0;
                total_gpu_time_ms = 0.0;
                metrics_count = 0;
            }

            // Map change handling
            std::string shm_map = packet.map_name;
            if (current_map != shm_map) {
                current_map = shm_map;
                if (!current_map.empty()) {
                    std::cout << "FC2 CHAMS V2: Map change detected: " << current_map << std::endl;
                    
                    // Clear previous map geometries immediately
                    depth_prepass.clear_geometry();

                    map_loading = true;
                    std::string map_name_copy = current_map;
                    map_load_future = std::async(std::launch::async, [map_name_copy]() {
                        AsyncMapResult result;
                        result.mesh = MapParser::LoadMesh(map_name_copy);
                        return result;
                    });
                } else {
                    std::cout << "FC2 CHAMS V2: Left map, clearing map geometries." << std::endl;
                    depth_prepass.clear_geometry();
                    map_loading = false;
                }
            }
        }

        // View matrix resolution (optional extrapolation)
        float latest_view_matrix[16];
        bool has_matrix = shm.read_view_matrix(latest_view_matrix);
        float render_view_matrix[16];
        if (cfg.extrapolate) {
            static ViewMatrixExtrapolator extrapolator;
            if (has_matrix) {
                extrapolator.update(latest_view_matrix);
            } else {
                extrapolator.update(packet.view_matrix);
            }
            extrapolator.get_extrapolated_matrix(render_view_matrix);
        } else {
            if (has_matrix) {
                std::memcpy(render_view_matrix, latest_view_matrix, sizeof(float) * 16);
            } else {
                std::memcpy(render_view_matrix, packet.view_matrix, sizeof(float) * 16);
            }
        }

        static double last_frame_time = glfwGetTime();
        double current_frame_time = glfwGetTime();
        float dt = static_cast<float>(current_frame_time - last_frame_time);
        last_frame_time = current_frame_time;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 0.15f) dt = 0.15f;

        // Transpose row-major View-Proj to column-major for OpenGL shaders
        float gl_vp[16];
        for (int c = 0; c < 4; c++) {
            gl_vp[c * 4 + 0] = render_view_matrix[0 * 4 + c];
            gl_vp[c * 4 + 1] = render_view_matrix[1 * 4 + c];
            gl_vp[c * 4 + 2] = render_view_matrix[2 * 4 + c];
            gl_vp[c * 4 + 3] = render_view_matrix[3 * 4 + c];
        }

        Vec3 cam_pos = get_camera_position(render_view_matrix);
        float cam_pos_arr[3] = { cam_pos.x, cam_pos.y, cam_pos.z };

        // Detailed debug logging throttled to 5 seconds
        static auto last_detail_print = std::chrono::steady_clock::now();
        auto now_detail = std::chrono::steady_clock::now();
        if (cfg.debug_bridge && std::chrono::duration_cast<std::chrono::seconds>(now_detail - last_detail_print).count() >= 5) {
            last_detail_print = now_detail;
            std::cout << "[DEBUG LOG] Window Size: " << width << "x" << height << std::endl;
            std::cout << "[DEBUG LOG] Camera Pos: (" << cam_pos.x << ", " << cam_pos.y << ", " << cam_pos.z << ")" << std::endl;
            std::cout << "[DEBUG LOG] ViewProj Matrix: ";
            for (int i = 0; i < 16; ++i) std::cout << render_view_matrix[i] << " ";
            std::cout << std::endl;
            
            std::cout << "[DEBUG LOG] Players Tracked: " << packet.player_count << std::endl;
            for (int i = 0; i < std::min(packet.player_count, 3); ++i) {
                const auto& p = packet.players[i];
                std::cout << "[DEBUG LOG] Player " << i << ": active=" << p.active 
                          << " team=" << p.team 
                          << " hp=" << p.health
                          << " name=" << p.model_name
                          << " origin=(" << p.origin.x << ", " << p.origin.y << ", " << p.origin.z << ")"
                          << " bone_count=" << p.bone_count << std::endl;
                
                if (p.active) {
                    if (p.bone_count > 0) {
                        std::cout << "            Bone 0 Pos: (" << p.bones[0].position.x << ", " << p.bones[0].position.y << ", " << p.bones[0].position.z << ")" << std::endl;
                    }
                    if (p.bone_count > 7) {
                        std::cout << "            Bone 7 Pos: (" << p.bones[7].position.x << ", " << p.bones[7].position.y << ", " << p.bones[7].position.z << ")" << std::endl;
                    }
                    
                    float sx = 0.0f, sy = 0.0f;
                    bool screen_ok = world_to_screen(p.origin, &sx, &sy, render_view_matrix, width, height);
                    std::cout << "            WorldToScreen(Origin): ok=" << (screen_ok ? "true" : "false")
                              << " screen=(" << sx << ", " << sy << ")" << std::endl;
                }
            }
        }

        auto gpu_start = std::chrono::high_resolution_clock::now();
        overlay.begin_frame();

        // 1. Run Map Depth Prepass (if active and loaded)
        bool has_prepass = cfg.use_depth_prepass && depth_prepass.has_geometry();
        if (has_prepass) {
            depth_prepass.render(gl_vp);
        }

        // 2. Render Player Chams
        struct RenderPalette {
            const CachedModel* model;
            std::vector<source2::Mat3x4> skinning_palette;
            bool is_visible;
            std::vector<Vec3> sanitized_bones;
        };

        static std::vector<RenderPalette> render_palettes;
        static std::vector<int> player_ubo_slots;
        static std::vector<int> sorted_player_indices;
        static std::vector<float> dummy_vis;

        auto cpu_start = std::chrono::high_resolution_clock::now();
        if (has_new_packet) {
            dummy_vis.assign(packet.player_count * 128, 1.0f);

            render_palettes.resize(packet.player_count);
            for (int i = 0; i < packet.player_count; ++i) {
                const auto& player = packet.players[i];
                if (!player.active) {
                    render_palettes[i].model = nullptr;
                    continue;
                }

                std::string model_key = player.model_name;
                if (player.has_defuser) {
                    model_key += "#defuser";
                }

                const auto* model = model_cache.get_or_load(model_key);
                if (!model || !model->valid) {
                    render_palettes[i].model = nullptr;
                    continue;
                }

                // Convert raw bone transforms to 3x4 world matrices
                std::vector<source2::Mat3x4> world_bones(model->mesh.bone_count);
                for (int j = 0; j < model->mesh.bone_count; ++j) {
                    if (j < player.bone_count) {
                        world_bones[j] = matrix_from_bone(player.bones[j].position, player.bones[j].rotation);
                    } else {
                        source2::Mat3x4 identity{};
                        identity.mat[0][0] = identity.mat[1][1] = identity.mat[2][2] = 1.0f;
                        world_bones[j] = identity;
                    }
                }

                // Sanitise bones for LOD issues
                sanitize_lod_bones(world_bones.data(), model->mesh.bone_count, model->mesh);

                // Extract sanitized world positions for ESP logic (fixes game LOD issues)
                std::vector<Vec3> sanitized_bones(model->mesh.bone_count);
                for (int j = 0; j < model->mesh.bone_count; ++j) {
                    sanitized_bones[j] = {
                        world_bones[j].mat[0][3],
                        world_bones[j].mat[1][3],
                        world_bones[j].mat[2][3]
                    };
                }

                // Multiply by inverse bind poses to build final skinned joint palette
                std::vector<source2::Mat3x4> skinning_palette(model->mesh.bone_count);
                for (int j = 0; j < model->mesh.bone_count; ++j) {
                    if (j < static_cast<int>(model->mesh.inv_bind_poses.size())) {
                        skinning_palette[j] = multiply_3x4(world_bones[j], model->mesh.inv_bind_poses[j]);
                    } else {
                        skinning_palette[j] = world_bones[j];
                    }
                }

                bool is_player_visible = true;

                render_palettes[i] = { model, skinning_palette, is_player_visible, sanitized_bones };
            }

            // 2.5 Batch convert and upload bone matrices to UBO
            std::vector<float> batch_bones;
            batch_bones.reserve(packet.player_count * 128 * 16);
            player_ubo_slots.assign(packet.player_count, -1);
            int current_slot = 0;

            for (int i = 0; i < packet.player_count; ++i) {
                const auto& rp = render_palettes[i];
                if (!rp.model) continue;

                float mat4_array[128 * 16];
                std::memset(mat4_array, 0, sizeof(mat4_array));

                for (size_t j = 0; j < rp.skinning_palette.size() && j < 128; ++j) {
                    const auto& b = rp.skinning_palette[j];
                    float* dest = &mat4_array[j * 16];
                    // Column 0
                    dest[0] = b.mat[0][0]; dest[1] = b.mat[1][0]; dest[2] = b.mat[2][0]; dest[3] = 0.0f;
                    // Column 1
                    dest[4] = b.mat[0][1]; dest[5] = b.mat[1][1]; dest[6] = b.mat[2][1]; dest[7] = 0.0f;
                    // Column 2
                    dest[8] = b.mat[0][2]; dest[9] = b.mat[1][2]; dest[10] = b.mat[2][2]; dest[11] = 0.0f;
                    // Column 3
                    dest[12] = b.mat[0][3]; dest[13] = b.mat[1][3]; dest[14] = b.mat[2][3]; dest[15] = 1.0f;
                }
                for (size_t j = rp.skinning_palette.size(); j < 128; ++j) {
                    float* dest = &mat4_array[j * 16];
                    dest[0] = dest[5] = dest[10] = dest[15] = 1.0f;
                }

                batch_bones.insert(batch_bones.end(), mat4_array, mat4_array + 128 * 16);
                player_ubo_slots[i] = current_slot++;
            }

            if (current_slot > 0) {
                chams_renderer.upload_bones_batch(batch_bones.data(), current_slot);
            }

            // Sort active player indices by model VAO to optimize state changes
            sorted_player_indices.clear();
            sorted_player_indices.reserve(packet.player_count);
            for (int i = 0; i < packet.player_count; ++i) {
                if (render_palettes[i].model) {
                    sorted_player_indices.push_back(i);
                }
            }
            std::sort(sorted_player_indices.begin(), sorted_player_indices.end(), [&](int a, int b) {
                return render_palettes[a].model->vao < render_palettes[b].model->vao;
            });
        }
        auto cpu_end = std::chrono::high_resolution_clock::now();
        double current_cpu_ms = std::chrono::duration<double, std::milli>(cpu_end - cpu_start).count();

        bool run_glow = (cfg.glow_enabled && cfg.outline_mode == "glow") || (cfg.esp_enabled && cfg.esp_skeleton && cfg.esp_skeleton_glow);
        if (run_glow) {
            chams_renderer.begin_glow_pass(width, height, gl_vp, cam_pos_arr);
            
            if (cfg.glow_enabled && cfg.outline_mode == "glow") {
                size_t idx = 0;
                while (idx < sorted_player_indices.size()) {
                    int first_player_idx = sorted_player_indices[idx];
                    const auto& first_rp = render_palettes[first_player_idx];
                    
                    if (!(first_rp.is_visible || cfg.show_invisible)) {
                        idx++;
                        continue;
                    }

                    unsigned int batch_vao = first_rp.model->vao;
                    unsigned int batch_ibo = first_rp.model->ibo;
                    size_t batch_index_count = first_rp.model->index_count;

                    int ubo_slots[8];
                    float colors[8 * 4];
                    int count = 0;
                    int base_slot = -1;

                    size_t next_idx = idx;
                    while (next_idx < sorted_player_indices.size() && count < 8) {
                        int p_idx = sorted_player_indices[next_idx];
                        const auto& rp = render_palettes[p_idx];

                        if (rp.model->vao != batch_vao) {
                            break;
                        }

                        if (!(rp.is_visible || cfg.show_invisible)) {
                            next_idx++;
                            continue;
                        }

                        int slot = player_ubo_slots[p_idx];
                        if (base_slot < 0) {
                            base_slot = slot;
                        }

                        int current_min = base_slot;
                        int current_max = base_slot;
                        for (int k = 0; k < count; ++k) {
                            if (ubo_slots[k] < current_min) current_min = ubo_slots[k];
                            if (ubo_slots[k] > current_max) current_max = ubo_slots[k];
                        }
                        int next_min = std::min(current_min, slot);
                        int next_max = std::max(current_max, slot);
                        if (next_max - next_min >= 8) {
                            break;
                        }

                        ubo_slots[count] = slot;

                        float custom_color[4];
                        if (cfg.glow_health_based) {
                            float hp_factor = static_cast<float>(packet.players[p_idx].health) / 100.0f;
                            if (hp_factor < 0.0f) hp_factor = 0.0f;
                            if (hp_factor > 1.0f) hp_factor = 1.0f;
                            for (int c = 0; c < 4; ++c) {
                                custom_color[c] = cfg.glow_health_end[c] + hp_factor * (cfg.glow_health_start[c] - cfg.glow_health_end[c]);
                            }
                        } else {
                            std::memcpy(custom_color, cfg.glow_color, sizeof(float) * 4);
                        }
                        std::memcpy(&colors[count * 4], custom_color, sizeof(float) * 4);

                        count++;
                        next_idx++;
                    }

                    if (count > 0) {
                        chams_renderer.render_glow_silhouette_instanced(batch_vao, batch_ibo, batch_index_count,
                                                                        ubo_slots, colors, count);
                    }
                    idx = next_idx;
                }
            }

            if (cfg.esp_enabled && cfg.esp_skeleton && cfg.esp_skeleton_glow) {
                GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
                glDrawBuffers(1, &drawBuffer);

                esp_renderer.clear();
                esp_renderer.set_projection(gl_vp);

                float pulse_factor = 1.0f;
                if (cfg.esp_skeleton_glow_pulse) {
                    float time = static_cast<float>(glfwGetTime());
                    pulse_factor = 0.625f + 0.375f * std::sin(time * cfg.esp_skeleton_glow_pulse_speed);
                }

                for (int i = 0; i < packet.player_count; ++i) {
                    const auto& rp = render_palettes[i];
                    if (!rp.model) continue;

                    float custom_glow_color[4];
                    const float* glow_color_ptr = nullptr;
                    if (cfg.esp_skeleton_glow_health_based) {
                        float hp_factor = static_cast<float>(packet.players[i].health) / 100.0f;
                        if (hp_factor < 0.0f) hp_factor = 0.0f;
                        if (hp_factor > 1.0f) hp_factor = 1.0f;

                        for (int c = 0; c < 4; ++c) {
                            custom_glow_color[c] = cfg.esp_skeleton_glow_health_end[c] + hp_factor * (cfg.esp_skeleton_glow_health_start[c] - cfg.esp_skeleton_glow_health_end[c]);
                        }
                        glow_color_ptr = custom_glow_color;
                    }

                    esp_renderer.add_skeleton_3d(rp.sanitized_bones, i, dummy_vis, cfg, true, pulse_factor, glow_color_ptr);
                }

                esp_renderer.flush_lines();

                GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
                glDrawBuffers(2, drawBuffers);
            }

            float current_thickness = cfg.glow_enabled ? cfg.glow_thickness : cfg.esp_skeleton_glow_thickness;
            float current_intensity = cfg.glow_enabled ? cfg.glow_intensity : cfg.esp_skeleton_glow_intensity;
            bool should_pulse = cfg.glow_enabled ? cfg.glow_pulse : cfg.esp_skeleton_glow_pulse;
            float pulse_speed = cfg.glow_enabled ? cfg.glow_pulse_speed : cfg.esp_skeleton_glow_pulse_speed;

            if (should_pulse) {
                float time = static_cast<float>(glfwGetTime());
                float factor = 0.625f + 0.375f * std::sin(time * pulse_speed);
                current_intensity *= factor;
            }
            chams_renderer.end_glow_pass(width, height, current_thickness, current_intensity);
        }

        // 1. Draw 3D Skeleton (drawn before body pass so it is underneath semi-transparent chams,
        // allowing chams to write depth correctly without occluding the skeleton)
        if (cfg.esp_enabled && cfg.esp_skeleton) {
            esp_renderer.clear();
            esp_renderer.set_projection(gl_vp);

            for (int i = 0; i < packet.player_count; ++i) {
                const auto& rp = render_palettes[i];
                if (!rp.model) continue;

                esp_renderer.add_skeleton_3d(rp.sanitized_bones, i, dummy_vis, cfg, false, 1.0f, nullptr);
            }

            if (has_prepass) {
                glEnable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);

                // Pass 1: Hidden
                glDepthFunc(GL_GREATER);
                esp_renderer.flush_lines_override(cfg.esp_skeleton_color_invis);

                // Pass 2: Visible
                glDepthFunc(GL_LEQUAL);
                esp_renderer.flush_lines_override(cfg.esp_skeleton_color_vis);

                glDepthMask(GL_TRUE);
                glDepthFunc(GL_LEQUAL);
            } else {
                glDisable(GL_DEPTH_TEST);
                esp_renderer.flush_lines();
            }
        }

        // Step 2: Body Pass (render chams overlay on screen)
        float current_glow_intensity = cfg.glow_intensity;
        if (cfg.glow_pulse) {
            float time = static_cast<float>(glfwGetTime());
            float factor = 0.625f + 0.375f * std::sin(time * cfg.glow_pulse_speed);
            current_glow_intensity *= factor;
        }

        chams_renderer.begin_body_pass(gl_vp, cam_pos_arr);

        auto draw_player_batches = [&](const std::vector<int>& target_indices, int style_id, const float* primary_color, const float* secondary_color) {
            if (target_indices.empty()) return;
            if (style_id <= 0 && !(cfg.glow_enabled && cfg.outline_mode == "stencil")) return;

            size_t idx = 0;
            while (idx < target_indices.size()) {
                int first_player_idx = target_indices[idx];
                const auto& first_rp = render_palettes[first_player_idx];

                unsigned int batch_vao = first_rp.model->vao;
                unsigned int batch_ibo = first_rp.model->ibo;
                size_t batch_index_count = first_rp.model->index_count;

                int ubo_slots[8];
                float colors[8 * 4];
                float glow_colors[8 * 4];
                int count = 0;
                int base_slot = -1;

                size_t next_idx = idx;
                while (next_idx < target_indices.size() && count < 8) {
                    int p_idx = target_indices[next_idx];
                    const auto& rp = render_palettes[p_idx];

                    if (rp.model->vao != batch_vao) {
                        break;
                    }

                    int slot = player_ubo_slots[p_idx];
                    if (base_slot < 0) {
                        base_slot = slot;
                    }

                    int current_min = base_slot;
                    int current_max = base_slot;
                    for (int k = 0; k < count; ++k) {
                        if (ubo_slots[k] < current_min) current_min = ubo_slots[k];
                        if (ubo_slots[k] > current_max) current_max = ubo_slots[k];
                    }
                    int next_min = std::min(current_min, slot);
                    int next_max = std::max(current_max, slot);
                    if (next_max - next_min >= 8) {
                        break;
                    }

                    ubo_slots[count] = slot;
                    std::memcpy(&colors[count * 4], primary_color, sizeof(float) * 4);
                    float resolved_outline_color[4];
                    if (cfg.glow_enabled && cfg.outline_mode == "stencil") {
                        if (cfg.glow_health_based) {
                            float hp_factor = static_cast<float>(packet.players[p_idx].health) / 100.0f;
                            if (hp_factor < 0.0f) hp_factor = 0.0f;
                            if (hp_factor > 1.0f) hp_factor = 1.0f;
                            for (int c = 0; c < 4; ++c) {
                                resolved_outline_color[c] = cfg.glow_health_end[c] + hp_factor * (cfg.glow_health_start[c] - cfg.glow_health_end[c]);
                            }
                        } else {
                            std::memcpy(resolved_outline_color, cfg.glow_color, sizeof(float) * 4);
                        }
                    } else if (secondary_color) {
                        std::memcpy(resolved_outline_color, secondary_color, sizeof(float) * 4);
                    } else {
                        float default_glow[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                        std::memcpy(resolved_outline_color, default_glow, sizeof(float) * 4);
                    }
                    std::memcpy(&glow_colors[count * 4], resolved_outline_color, sizeof(float) * 4);

                    count++;
                    next_idx++;
                }

                if (count > 0) {
                    float default_glow[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                    const float* pass_glow = secondary_color ? secondary_color : default_glow;
                    bool is_stencil_outline = cfg.glow_enabled && cfg.outline_mode == "stencil";
                    chams_renderer.render_mesh_instanced(
                        batch_vao, batch_ibo, batch_index_count,
                        style_id, pass_glow,
                        is_stencil_outline ? cfg.glow_thickness : 0.0f, current_glow_intensity, 0.0f,
                        cfg.flat_chams_no_overlap, ubo_slots, colors,
                        glow_colors, count,
                        is_stencil_outline
                    );
                }
                idx = next_idx;
            }
        };

        bool is_stencil_outline = cfg.glow_enabled && cfg.outline_mode == "stencil";
        if (has_prepass) {
            // Pixel-perfect depth prepass occlusion:
            // A) Render hidden pass: depth test = GL_GREATER, depth write disabled
            if ((style_invis_id > 0 || is_stencil_outline) && cfg.show_invisible) {
                glDepthFunc(GL_GREATER);
                glDepthMask(GL_FALSE);
                draw_player_batches(sorted_player_indices, style_invis_id, cfg.color_invis, cfg.color_invis_sec);
            }
            
            // B) Render visible pass: depth test = GL_LEQUAL, depth write enabled
            if (style_vis_id > 0 || is_stencil_outline) {
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_TRUE);
                draw_player_batches(sorted_player_indices, style_vis_id, cfg.color_vis, cfg.color_vis_sec);
            }
        } 
        else {
            std::vector<int> visible_indices;
            std::vector<int> invisible_indices;
            visible_indices.reserve(sorted_player_indices.size());
            invisible_indices.reserve(sorted_player_indices.size());
            for (int i : sorted_player_indices) {
                if (render_palettes[i].is_visible) {
                    visible_indices.push_back(i);
                } else {
                    invisible_indices.push_back(i);
                }
            }

            if (style_vis_id > 0 || is_stencil_outline) {
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_TRUE);
                draw_player_batches(visible_indices, style_vis_id, cfg.color_vis, cfg.color_vis_sec);
            }

            if ((style_invis_id > 0 || is_stencil_outline) && cfg.show_invisible) {
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_TRUE);
                draw_player_batches(invisible_indices, style_invis_id, cfg.color_invis, cfg.color_invis_sec);
            }
        }

        chams_renderer.end_body_pass();
        glDepthMask(GL_TRUE); // Restore depth writes

        // Render 3D Map Collision Wireframe Visualizer
        if (cfg.map_visualizer_enabled) {
            depth_prepass.render_wireframe(gl_vp, cfg.map_visualizer_color, cfg.map_visualizer_depth_tested);
        }

        // Render ESP Overlay (Skeleton, Box, Health Bar)
        if (cfg.esp_enabled) {
            static float smoothed_health[64] = { -1.0f };
            static bool player_active_last[64] = { false };

            // 3D Skeleton drawn before body pass

            // 2. Draw 2D Box & Health Bar
            if (cfg.esp_box || cfg.esp_health_bar) {
                glDisable(GL_DEPTH_TEST);
                esp_renderer.clear();
                esp_renderer.set_ortho(0, width, height, 0);

                for (int i = 0; i < packet.player_count; ++i) {
                    const auto& rp = render_palettes[i];
                    if (!rp.model) {
                        player_active_last[i] = false;
                        continue;
                    }
                    const auto& player = packet.players[i];

                    if (!player_active_last[i] || smoothed_health[i] < 0.0f) {
                        smoothed_health[i] = static_cast<float>(player.health);
                        player_active_last[i] = true;
                    } else {
                        smoothed_health[i] += (player.health - smoothed_health[i]) * std::clamp(10.0f * dt, 0.0f, 1.0f);
                        if (std::abs(player.health - smoothed_health[i]) > 50.0f) {
                            smoothed_health[i] = static_cast<float>(player.health);
                        }
                    }

                    float bx, by, bw, bh;
                    if (get_player_bounds(rp.sanitized_bones, player.origin, packet.local_eye, render_view_matrix, width, height, bx, by, bw, bh, cfg)) {
                        // Draw bounding box
                        if (cfg.esp_box) {
                            esp_renderer.add_outlined_rect_2d(bx, by, bw, bh, cfg.esp_box_color, cfg.esp_box_thickness, cfg.esp_box_outline);
                        }

                        // Draw health bar
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

        // 3. Draw 2D FPS Counter Overlay
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
            overlay.draw_fps(current_fps);
        }

        overlay.end_frame();

        // Render the ImGui settings menu window (throttled to ~60 FPS)
        static auto last_menu_render = std::chrono::steady_clock::now();
        auto now_menu = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now_menu - last_menu_render).count() >= 16) {
            menu.render();
            // After menu.render() switches to the menu context, invalidate our tracker
            current_context = menu.get_window();
            last_menu_render = now_menu;
        }
        auto gpu_end = std::chrono::high_resolution_clock::now();
        double current_gpu_ms = std::chrono::duration<double, std::milli>(gpu_end - gpu_start).count();

        if (has_new_packet) {
            total_ipc_time_ms += current_ipc_ms;
            total_cpu_time_ms += current_cpu_ms;
            total_gpu_time_ms += current_gpu_ms;
            metrics_count++;
        }

        // High-precision pacing limiter
        if (cfg.fps > 0) {
            auto frame_end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> elapsed = frame_end - frame_start;
            double target_ms = 1000.0 / cfg.fps;
            if (elapsed.count() < target_ms) {
                double remaining = target_ms - elapsed.count();
                if (remaining > 1.5) {
                    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(remaining - 1.5));
                }
                while (true) {
                    auto now = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::milli> spin_elapsed = now - frame_start;
                    if (spin_elapsed.count() >= target_ms) break;
                    std::this_thread::yield();
                }
            }
        }
    }



    esp_renderer.cleanup();

    shm.shutdown();
    std::cout << "FC2 CHAMS V2: Terminated cleanly." << std::endl;
    return 0;
}
