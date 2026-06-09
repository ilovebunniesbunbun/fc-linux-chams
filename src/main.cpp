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
#include "vischeck/bvh_parser.hpp"
#include "overlay_client.hpp"
#include "renderer/gl_loader.hpp"
#include "renderer/gpu_chams.hpp"
#include "renderer/depth_prepass.hpp"
#include "model_cache.hpp"
#include "vpk/vmdl/maps/map_parser.hpp"
#include "config.hpp"
#include "menu_client.hpp"

using json = nlohmann::json;

inline Vec3 operator-(const Vec3& a, const Vec3& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline Vec3 operator*(const Vec3& a, float b) {
    return { a.x * b, a.y * b, a.z * b };
}

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

int main() {
    std::cout << "FC2 CHAMS V2: Launching GPU-Skinned Linux Overlay..." << std::endl;

    OverlayConfig cfg = load_config("overlay.json");
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

    // Initialize dynamic OpenGL function pointers
    if (!load_gl_functions()) {
        std::cerr << "FC2 CHAMS V2: Failed to load modern OpenGL functions. Ensure OpenGL 3.3 support." << std::endl;
        return 1;
    }
    std::cout << "FC2 CHAMS V2: Loaded modern OpenGL core functions." << std::endl;

    MenuClient menu(cfg);

    // Make overlay context current again so shaders and buffers are compiled on the overlay window context
    glfwMakeContextCurrent(overlay.get_window());

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

    ModelCache model_cache;
    ShmReader shm;

    if (!shm.initialize()) {
        std::cerr << "FC2 CHAMS V2: POSIX Shared Memory segment not initialized. Start Lua collector first." << std::endl;
        return 1;
    }

    // Fallback BVH Raytracer
    LocalMapBVH bvh;
    std::shared_mutex bvh_mutex;
    bvh.load_mock_geometry();

    ShmPacket packet;
    std::string current_map = "";

    std::mutex visibility_mutex;
    std::vector<float> global_player_visibility;
    bool raytrace_in_progress = false;
    ShmPacket raytrace_packet;
    std::vector<std::vector<bool>> raytrace_bone_masks;

    bool first_packet = true;
    auto last_debug_print = std::chrono::steady_clock::now();
    uint32_t packet_count_since_print = 0;

    while (!overlay.should_close() && !menu.should_close()) {
        auto frame_start = std::chrono::high_resolution_clock::now();

        // Ensure the overlay context is current for all uploads and rendering
        glfwMakeContextCurrent(overlay.get_window());

        overlay.poll_events();

        shm.try_frame();

        // Dynamically resolve styles in real-time
        int style_vis_id = get_style_id(cfg.style_vis);
        int style_invis_id = get_style_id(cfg.style_invis);

        bool has_new_packet = shm.fetch_latest(packet);

        if (has_new_packet) {
            packet_count_since_print++;
            if (first_packet) {
                std::cout << "FC2 CHAMS V2: Shared memory bridge communication active." << std::endl;
                first_packet = false;
            }

            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_debug_print).count() >= 5) {
                std::cout << "FC2 CHAMS V2: Bridge active - received " << packet_count_since_print 
                          << " packets in last 5s (frame index: " << packet.frame_index 
                          << ", players tracked: " << packet.player_count << ")" << std::endl;
                last_debug_print = now;
                packet_count_since_print = 0;
            }

            // Map change handling
            std::string shm_map = packet.map_name;
            if (current_map != shm_map) {
                current_map = shm_map;
                if (!current_map.empty()) {
                    std::cout << "FC2 CHAMS V2: Map change detected: " << current_map << std::endl;
                    
                    // 1. Try to load VPK geometry for GPU Depth Prepass
                    auto load_start = std::chrono::high_resolution_clock::now();
                    MapParser::MapMesh map_mesh = MapParser::LoadMesh(current_map);
                    if (map_mesh.Valid && !map_mesh.Triangles.empty()) {
                        depth_prepass.upload_geometry(map_mesh.Triangles);
                        auto load_end = std::chrono::high_resolution_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(load_end - load_start).count();
                        std::cout << "FC2 CHAMS V2: Loaded VPK map geometry (" << map_mesh.Triangles.size() << " triangles) in " << duration << "ms." << std::endl;
                    } else {
                        std::cout << "FC2 CHAMS V2: VPK map geometry unavailable, clearing GPU prepass." << std::endl;
                        depth_prepass.clear_geometry();
                    }

                    // 2. Try to load BVH geometry for fallback raytracer
                    std::string map_path = cfg.maps_dir + "/" + current_map + ".tri";
                    std::unique_lock<std::shared_mutex> bvh_lock(bvh_mutex);
                    if (bvh.load_tri_file(map_path)) {
                        std::cout << "FC2 CHAMS V2: Loaded map fallback BVH (" << bvh.triangles.size() << " triangles)." << std::endl;
                    } else {
                        std::cerr << "FC2 CHAMS V2: Fallback BVH not found for " << current_map << ". Using mock shapes." << std::endl;
                        bvh.load_mock_geometry();
                    }
                } else {
                    std::cout << "FC2 CHAMS V2: Left map, clearing map geometries." << std::endl;
                    depth_prepass.clear_geometry();
                    std::unique_lock<std::shared_mutex> bvh_lock(bvh_mutex);
                    bvh.triangles.clear();
                    bvh.nodes.clear();
                    bvh.indices.clear();
                }
            }

            // Raytracing fallback trigger: run only if Depth Prepass lacks loaded geometry
            bool needs_cpu_raytrace = cfg.use_bvh_fallback && (!cfg.use_depth_prepass || !depth_prepass.has_geometry());
            if (needs_cpu_raytrace) {
                // Gather joint masks
                std::vector<std::vector<bool>> active_bone_masks(packet.player_count, std::vector<bool>(128, false));
                for (int i = 0; i < packet.player_count; ++i) {
                    const auto& player = packet.players[i];
                    if (player.active) {
                        const auto* cache_model = model_cache.get_or_load(player.model_name);
                        if (cache_model && cache_model->valid) {
                            // Only trace active joints to optimize CPU usage
                            for (size_t j = 0; j < cache_model->mesh.inv_bind_poses.size() && j < 128; ++j) {
                                active_bone_masks[i][j] = true;
                            }
                        } else {
                            std::fill(active_bone_masks[i].begin(), active_bone_masks[i].end(), true);
                        }
                    }
                }

                // Fire async raytracer thread
                std::lock_guard<std::mutex> lock(visibility_mutex);
                if (!raytrace_in_progress) {
                    raytrace_in_progress = true;
                    raytrace_packet = packet;
                    raytrace_bone_masks = std::move(active_bone_masks);

                    std::thread([&bvh, &bvh_mutex, &visibility_mutex, &raytrace_in_progress, &global_player_visibility, &raytrace_packet, &raytrace_bone_masks]() {
                        ShmPacket local_packet;
                        std::vector<std::vector<bool>> local_bone_masks;
                        {
                            std::lock_guard<std::mutex> lock(visibility_mutex);
                            local_packet = raytrace_packet;
                            local_bone_masks = raytrace_bone_masks;
                        }

                        std::vector<float> local_vis(local_packet.player_count * 128, 0.0f);
                        std::vector<std::future<void>> futures;

                        for (int i = 0; i < local_packet.player_count; ++i) {
                            futures.push_back(std::async(std::launch::async, [&bvh, &bvh_mutex, &local_packet, &local_vis, &local_bone_masks, i]() {
                                const auto& player = local_packet.players[i];
                                if (!player.active) {
                                    for (int b = 0; b < 128; ++b) local_vis[i * 128 + b] = 0.0f;
                                    return;
                                }

                                std::shared_lock<std::shared_mutex> bvh_lock(bvh_mutex);
                                Vec3 local_eye = local_packet.local_eye;

                                for (int b = 0; b < 128; ++b) {
                                    if (!local_bone_masks[i][b]) {
                                        local_vis[i * 128 + b] = 1.0f;
                                        continue;
                                    }

                                    Vec3 target_pos = player.bones[b].position;
                                    Vec3 delta = target_pos - local_eye;
                                    float dist = std::sqrt(delta.x*delta.x + delta.y*delta.y + delta.z*delta.z);
                                    if (dist > 12.0f) {
                                        Vec3 dir = delta * (1.0f / dist);
                                        float shrink = std::min(4.0f, dist * 0.04f);
                                        target_pos = target_pos - dir * shrink;
                                    }
                                    TraceResult trace = bvh.trace_ray(local_eye, target_pos);
                                    local_vis[i * 128 + b] = !trace.hit ? 1.0f : 0.0f;
                                }

                                // Handle bone fallbacks
                                for (int b = 0; b < 128; ++b) {
                                    if (!local_bone_masks[i][b]) {
                                        local_vis[i * 128 + b] = local_vis[i * 128 + 6]; // Neck default
                                    }
                                }
                            }));
                        }

                        for (auto& f : futures) {
                            f.get();
                        }

                        {
                            std::lock_guard<std::mutex> lock(visibility_mutex);
                            global_player_visibility = std::move(local_vis);
                            raytrace_in_progress = false;
                        }
                    }).detach();
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

        overlay.begin_frame();

        // 1. Run Map Depth Prepass (if active and loaded)
        bool has_prepass = cfg.use_depth_prepass && depth_prepass.has_geometry();
        if (has_prepass) {
            depth_prepass.render(gl_vp);
        }

        // 2. Render Player Chams
        // Capture raytrace visibility safely
        std::vector<float> rendering_vis;
        {
            std::lock_guard<std::mutex> lock(visibility_mutex);
            rendering_vis = global_player_visibility;
        }

        struct RenderPalette {
            const CachedModel* model;
            std::vector<source2::Mat3x4> skinning_palette;
            bool is_visible;
        };

        std::vector<RenderPalette> render_palettes(packet.player_count);
        for (int i = 0; i < packet.player_count; ++i) {
            const auto& player = packet.players[i];
            if (!player.active) {
                render_palettes[i].model = nullptr;
                continue;
            }

            const auto* model = model_cache.get_or_load(player.model_name);
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
            if (!has_prepass) {
                int head_idx = i * 128 + 6; // head/neck joint
                if (head_idx < static_cast<int>(rendering_vis.size())) {
                    is_player_visible = (rendering_vis[head_idx] > 0.5f);
                }
            }

            render_palettes[i] = { model, skinning_palette, is_player_visible };
        }

        // Step 1: Glow Pass (render silhouettes and masks to FBO, blur, and composite)
        if (cfg.glow_enabled) {
            chams_renderer.begin_glow_pass(width, height, gl_vp, cam_pos_arr);
            for (int i = 0; i < packet.player_count; ++i) {
                const auto& rp = render_palettes[i];
                if (!rp.model) continue;

                if (rp.is_visible || cfg.show_invisible) {
                    chams_renderer.render_glow_silhouette(rp.model->vao, rp.model->ibo, rp.model->index_count,
                                                          rp.skinning_palette, cfg.glow_color);
                }
            }

            float current_intensity = cfg.glow_intensity;
            if (cfg.glow_pulse) {
                float time = static_cast<float>(glfwGetTime());
                float factor = 0.625f + 0.375f * std::sin(time * cfg.glow_pulse_speed);
                current_intensity *= factor;
            }
            chams_renderer.end_glow_pass(width, height, cfg.glow_thickness, current_intensity);
        }

        // Step 2: Body Pass (render chams overlay on screen)
        chams_renderer.begin_body_pass(gl_vp, cam_pos_arr);
        for (int i = 0; i < packet.player_count; ++i) {
            const auto& rp = render_palettes[i];
            if (!rp.model) continue;

            if (has_prepass) {
                // Pixel-perfect depth prepass occlusion:
                // A) Render hidden pass: depth test = GL_GREATER, depth write disabled
                if (style_invis_id > 0 && cfg.show_invisible) {
                    glDepthFunc(GL_GREATER);
                    glDepthMask(GL_FALSE);
                    chams_renderer.render_mesh(rp.model->vao, rp.model->ibo, rp.model->index_count, 
                                               rp.skinning_palette, cfg.color_invis, style_invis_id,
                                               nullptr, 0.0f, 0.0f);
                }
                
                // B) Render visible pass: depth test = GL_LEQUAL, depth write enabled
                if (style_vis_id > 0) {
                    glDepthFunc(GL_LEQUAL);
                    glDepthMask(GL_TRUE);
                    chams_renderer.render_mesh(rp.model->vao, rp.model->ibo, rp.model->index_count, 
                                               rp.skinning_palette, cfg.color_vis, style_vis_id,
                                               nullptr, 0.0f, 0.0f);
                }
            } 
            else {
                glDepthFunc(GL_LEQUAL);
                glDepthMask(GL_TRUE);
                if (rp.is_visible) {
                    if (style_vis_id > 0) {
                        chams_renderer.render_mesh(rp.model->vao, rp.model->ibo, rp.model->index_count,
                                                   rp.skinning_palette, cfg.color_vis, style_vis_id,
                                                   nullptr, 0.0f, 0.0f);
                    }
                } else {
                    if (style_invis_id > 0 && cfg.show_invisible) {
                        chams_renderer.render_mesh(rp.model->vao, rp.model->ibo, rp.model->index_count,
                                                   rp.skinning_palette, cfg.color_invis, style_invis_id,
                                                   nullptr, 0.0f, 0.0f);
                    }
                }
            }
        }
        chams_renderer.end_body_pass();

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

        // Render the ImGui settings menu window
        menu.render();

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

    shm.shutdown();
    std::cout << "FC2 CHAMS V2: Terminated cleanly." << std::endl;
    return 0;
}
