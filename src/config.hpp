#pragma once
#include <string>
#include <vector>

struct OverlayConfig {
    int monitor_w = 2560;
    int monitor_h = 1440;
    int game_w = 1280;
    int game_h = 960;
    std::string scaling = "";
    int game_x = 0;
    int game_y = 0;
    int fps = 0;
    bool show_fps = true;
    bool vsync = false;
    bool extrapolate = false;
    float color_vis[4] = {0.0f, 1.0f, 0.0f, 0.65f};
    float color_vis_sec[4] = {1.0f, 1.0f, 0.0f, 0.8f};
    float color_invis[4] = {1.0f, 0.0f, 0.0f, 0.65f};
    float color_invis_sec[4] = {1.0f, 0.5f, 0.0f, 0.8f};
    bool show_invisible = true;
    std::string maps_dir = "./maps";
    bool hyprland_support = false;

    // V2 parameters
    std::string vpk_path = "auto";
    std::string style_vis = "metallic";
    std::string style_invis = "flat";
    bool use_depth_prepass = true;
    bool use_bvh_fallback = true;
    bool flat_chams_no_overlap = false;

    // Glow shader options
    bool glow_enabled = false;
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
    bool esp_skeleton = true;
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
    bool esp_box = true;
    float esp_box_thickness = 1.5f;
    float esp_box_color[4] = {1.0f, 1.0f, 1.0f, 0.8f};
    bool esp_box_outline = true;
    int esp_box_mode = 1; // 0 = Dynamic (Bones), 1 = Dynamic (Feet + Head), 2 = Static
    float esp_box_static_w = 41500.0f;
    float esp_box_static_h = 76200.0f;
    bool esp_health_bar = true;
    bool esp_health_bar_gradient = true;
    float esp_health_bar_color[4] = {0.0f, 1.0f, 0.0f, 0.8f};
    float esp_health_bar_gradient_start[4] = {0.0f, 1.0f, 0.0f, 0.8f};
    float esp_health_bar_gradient_end[4] = {1.0f, 0.0f, 0.0f, 0.8f};
    bool esp_health_bar_outline = true;
    float esp_health_bar_thickness = 2.0f;
    bool debug_bridge = false;
};

OverlayConfig load_config(const std::string& filename);
int get_style_id(const std::string& name);
void save_config(const std::string& filename, const OverlayConfig& cfg);
