#pragma once
#include <string>
#include <vector>

struct GpuDevice {
    std::string name;
    std::string vendor_id;
    std::string device_id;
    std::string driver;
    std::string display_name;
};

std::vector<GpuDevice> detect_gpus();

struct OverlayConfig {
    int monitor_w = 2560;
    int monitor_h = 1440;
    int game_w = 1280;
    int game_h = 960;
    std::string scaling = "";
    int game_x = 0;
    int game_y = 0;
    int fps = 143;
    bool show_fps = true;
    bool vsync = false;
    bool extrapolate = false;
    float color_vis[4] = {0.0f, 1.0f, 0.0f, 209.0f / 255.0f};
    float color_vis_sec[4] = {1.0f, 1.0f, 0.0f, 0.8f};
    float color_invis[4] = {1.0f, 0.0f, 0.0f, 224.0f / 255.0f};
    float color_invis_sec[4] = {1.0f, 0.5f, 0.0f, 0.8f};
    bool show_invisible = true;
    std::string maps_dir = "./maps";
    bool hyprland_support = false;
    int monitor_index = 0;
    std::string gpu_preference = "default";

    // V2 parameters
    std::string vpk_path = "auto";
    std::string style_vis = "flat";
    std::string style_invis = "flat";
    bool use_depth_prepass = true;
    bool use_bvh_fallback = true;
    bool flat_chams_no_overlap = true;

    // Glow shader options
    bool glow_enabled = false;
    std::string outline_mode = "glow"; // "glow" or "stencil"
    bool glow_health_based = false;
    float glow_color[4] = {1.0f, 0.0f, 1.0f, 0.8f};
    float glow_health_start[4] = {0.0f, 1.0f, 0.0f, 0.8f};
    float glow_health_end[4] = {1.0f, 0.0f, 0.0f, 0.8f};
    float glow_thickness = 1.5f;
    float glow_intensity = 1.0f;
    bool glow_pulse = false;
    float glow_pulse_speed = 2.0f;

    // ESP parameters
    bool esp_enabled = true;
    bool esp_skeleton = false;
    bool esp_rounded_skeleton = false;
    float esp_skeleton_thickness = 1.5f;
    float esp_skeleton_color_vis[4] = {0.0f, 1.0f, 0.0f, 0.8f};
    float esp_skeleton_color_invis[4] = {1.0f, 0.0f, 0.0f, 0.8f};
    bool esp_skeleton_glow = false;
    float esp_skeleton_glow_color[4] = {0.0f, 1.0f, 1.0f, 0.8f};
    bool esp_skeleton_glow_pulse = false;
    float esp_skeleton_glow_pulse_speed = 2.0f;
    float esp_skeleton_glow_intensity = 1.0f;
    float esp_skeleton_glow_thickness = 1.5f;
    bool esp_skeleton_glow_health_based = false;
    float esp_skeleton_glow_health_start[4] = {0.0f, 1.0f, 0.0f, 0.8f};
    float esp_skeleton_glow_health_end[4] = {1.0f, 0.0f, 0.0f, 0.8f};
    bool esp_box = false;
    float esp_box_thickness = 1.5f;
    float esp_box_color[4] = {1.0f, 1.0f, 1.0f, 0.8f};
    bool esp_box_outline = true;
    int esp_box_mode = 1; // 0 = Dynamic (Bones), 1 = Dynamic (Feet + Head), 2 = Static
    float esp_box_static_w = 41500.0f;
    float esp_box_static_h = 76200.0f;
    bool esp_health_bar = false;
    bool esp_health_bar_gradient = true;
    float esp_health_bar_color[4] = {0.0f, 1.0f, 0.0f, 0.8f};
    float esp_health_bar_gradient_start[4] = {0.0f, 1.0f, 0.0f, 0.8f};
    float esp_health_bar_gradient_end[4] = {1.0f, 0.0f, 0.0f, 0.8f};
    bool esp_health_bar_outline = true;
    float esp_health_bar_thickness = 2.0f;
    bool debug_bridge = false;
    bool map_visualizer_enabled = false;
    bool map_visualizer_depth_tested = true;
    float map_visualizer_color[4] = {0.0f, 0.784f, 0.392f, 0.47f};

    // V3 parameters (Grenade)
    bool draw_grenade_trajectory = true;
    float grenade_trajectory_color[4] = {0.67f, 0.69f, 0.86f, 0.8f};
    float trajectory_bounce_color[4] = {0.76f, 0.78f, 0.84f, 1.0f};
    float trajectory_detonation_color[4] = {0.55f, 0.59f, 0.92f, 1.0f};
    float trajectory_thickness = 2.0f;
    float trajectory_bounce_size = 2.0f;
    float trajectory_detonation_radius = 15.0f;
    float trajectory_fade_time = 1.5f;
    bool trajectory_show_through_walls = false;
};

OverlayConfig load_config(const std::string& filename);
int get_style_id(const std::string& name);
void save_config(const std::string& filename, const OverlayConfig& cfg);
