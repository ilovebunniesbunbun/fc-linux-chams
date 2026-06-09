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
    bool extrapolate = false;
    float color_vis[4] = {0.0f, 1.0f, 0.0f, 0.65f};
    float color_invis[4] = {1.0f, 0.0f, 0.0f, 0.65f};
    bool show_invisible = true;
    std::string maps_dir = "./maps";
    bool hyprland_support = false;

    // V2 parameters
    std::string vpk_path = "auto";
    std::string style_vis = "metallic";
    std::string style_invis = "flat";
    bool use_depth_prepass = true;
    bool use_bvh_fallback = true;

    // Glow shader options
    bool glow_enabled = false;
    float glow_color[4] = {1.0f, 0.0f, 1.0f, 0.8f};
    float glow_thickness = 1.5f;
    float glow_intensity = 1.0f;
    bool glow_pulse = false;
    float glow_pulse_speed = 2.0f;
};

OverlayConfig load_config(const std::string& filename);
int get_style_id(const std::string& name);
void save_config(const std::string& filename, const OverlayConfig& cfg);
