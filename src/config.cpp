#include "config.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

OverlayConfig load_config(const std::string& filename) {
    OverlayConfig cfg;
    std::string path = filename;
    std::ifstream file(path);
    if (!file.is_open()) {
        // Try parent directory
        path = "../" + filename;
        file.open(path);
    }
    if (!file.is_open()) {
        std::ofstream outfile(filename);
        if (outfile.is_open()) {
            json default_json = {
                {"monitor_w", 2560},
                {"monitor_h", 1440},
                {"game_w", 1280},
                {"game_h", 960},
                {"scaling", "stretched"},
                {"game_x", 0},
                {"game_y", 0},
                {"fps", 0},
                {"show_fps", true},
                {"extrapolate", false},
                {"color_visible", {0, 255, 0, 166}},
                {"color_invisible", {255, 0, 0, 166}},
                {"show_invisible", true},
                {"maps_dir", "./maps"},
                {"hyprland_support", false},
                {"vpk_path", "auto"},
                {"chams_style_visible", "metallic"},
                {"chams_style_hidden", "flat"},
                {"use_depth_prepass", true},
                {"use_bvh_fallback", true},
                {"glow_enabled", false},
                {"glow_thickness", 1.5},
                {"glow_intensity", 1.0},
                {"glow_color", {255, 0, 255, 204}},
                {"glow_pulse", false},
                {"glow_pulse_speed", 2.0}
            };
            outfile << default_json.dump(4) << std::endl;
            outfile.close();
        }
        return cfg;
    }

    try {
        json j;
        file >> j;
        file.close();

        if (j.contains("monitor_w")) cfg.monitor_w = j["monitor_w"].get<int>();
        if (j.contains("monitor_h")) cfg.monitor_h = j["monitor_h"].get<int>();
        if (j.contains("game_w")) cfg.game_w = j["game_w"].get<int>();
        if (j.contains("game_h")) cfg.game_h = j["game_h"].get<int>();
        if (j.contains("scaling")) cfg.scaling = j["scaling"].get<std::string>();
        if (j.contains("game_x")) cfg.game_x = j["game_x"].get<int>();
        if (j.contains("game_y")) cfg.game_y = j["game_y"].get<int>();
        if (j.contains("fps")) cfg.fps = j["fps"].get<int>();
        if (j.contains("show_fps")) cfg.show_fps = j["show_fps"].get<bool>();
        if (j.contains("extrapolate")) cfg.extrapolate = j["extrapolate"].get<bool>();
        if (j.contains("maps_dir")) cfg.maps_dir = j["maps_dir"].get<std::string>();
        if (j.contains("show_invisible")) cfg.show_invisible = j["show_invisible"].get<bool>();
        if (j.contains("hyprland_support")) cfg.hyprland_support = j["hyprland_support"].get<bool>();

        if (j.contains("color_visible") && j["color_visible"].is_array() && j["color_visible"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.color_vis[i] = j["color_visible"][i].get<float>() / 255.0f;
            }
        }
        if (j.contains("color_invisible") && j["color_invisible"].is_array() && j["color_invisible"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.color_invis[i] = j["color_invisible"][i].get<float>() / 255.0f;
            }
        }

        if (j.contains("vpk_path")) cfg.vpk_path = j["vpk_path"].get<std::string>();
        if (j.contains("chams_style_visible")) cfg.style_vis = j["chams_style_visible"].get<std::string>();
        if (j.contains("chams_style_hidden")) cfg.style_invis = j["chams_style_hidden"].get<std::string>();
        if (j.contains("use_depth_prepass")) cfg.use_depth_prepass = j["use_depth_prepass"].get<bool>();
        if (j.contains("use_bvh_fallback")) cfg.use_bvh_fallback = j["use_bvh_fallback"].get<bool>();

        if (j.contains("glow_enabled")) cfg.glow_enabled = j["glow_enabled"].get<bool>();
        if (j.contains("glow_thickness")) cfg.glow_thickness = j["glow_thickness"].get<float>();
        if (j.contains("glow_intensity")) cfg.glow_intensity = j["glow_intensity"].get<float>();
        if (j.contains("glow_pulse")) cfg.glow_pulse = j["glow_pulse"].get<bool>();
        if (j.contains("glow_pulse_speed")) cfg.glow_pulse_speed = j["glow_pulse_speed"].get<float>();

        if (j.contains("glow_color") && j["glow_color"].is_array() && j["glow_color"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.glow_color[i] = j["glow_color"][i].get<float>() / 255.0f;
            }
        }
    } catch (...) {}

    if (cfg.scaling.empty()) cfg.scaling = "stretched";
    return cfg;
}

int get_style_id(const std::string& name) {
    if (name == "textured") return 1;
    if (name == "flat") return 2;
    if (name == "metallic" || name == "fresnel") return 3;
    if (name == "glow_blend") return 5;
    if (name == "glow" || name == "cs2_glow") return 6;
    return 0; // Disabled
}

void save_config(const std::string& filename, const OverlayConfig& cfg) {
    json j = {
        {"monitor_w", cfg.monitor_w},
        {"monitor_h", cfg.monitor_h},
        {"game_w", cfg.game_w},
        {"game_h", cfg.game_h},
        {"scaling", cfg.scaling},
        {"game_x", cfg.game_x},
        {"game_y", cfg.game_y},
        {"fps", cfg.fps},
        {"show_fps", cfg.show_fps},
        {"extrapolate", cfg.extrapolate},
        {"color_visible", {
            static_cast<int>(cfg.color_vis[0] * 255.0f),
            static_cast<int>(cfg.color_vis[1] * 255.0f),
            static_cast<int>(cfg.color_vis[2] * 255.0f),
            static_cast<int>(cfg.color_vis[3] * 255.0f)
        }},
        {"color_invisible", {
            static_cast<int>(cfg.color_invis[0] * 255.0f),
            static_cast<int>(cfg.color_invis[1] * 255.0f),
            static_cast<int>(cfg.color_invis[2] * 255.0f),
            static_cast<int>(cfg.color_invis[3] * 255.0f)
        }},
        {"show_invisible", cfg.show_invisible},
        {"maps_dir", cfg.maps_dir},
        {"hyprland_support", cfg.hyprland_support},
        {"vpk_path", cfg.vpk_path},
        {"chams_style_visible", cfg.style_vis},
        {"chams_style_hidden", cfg.style_invis},
        {"use_depth_prepass", cfg.use_depth_prepass},
        {"use_bvh_fallback", cfg.use_bvh_fallback},
        {"glow_enabled", cfg.glow_enabled},
        {"glow_thickness", cfg.glow_thickness},
        {"glow_intensity", cfg.glow_intensity},
        {"glow_pulse", cfg.glow_pulse},
        {"glow_pulse_speed", cfg.glow_pulse_speed},
        {"glow_color", {
            static_cast<int>(cfg.glow_color[0] * 255.0f),
            static_cast<int>(cfg.glow_color[1] * 255.0f),
            static_cast<int>(cfg.glow_color[2] * 255.0f),
            static_cast<int>(cfg.glow_color[3] * 255.0f)
        }}
    };

    // Try saving in current directory first
    std::ofstream file(filename);
    if (!file.is_open()) {
        // Fall back to parent directory
        file.open("../" + filename);
    }
    if (file.is_open()) {
        file << j.dump(4) << std::endl;
        file.close();
        std::cout << "FC2 CHAMS V2: Saved config successfully." << std::endl;
    } else {
        std::cerr << "FC2 CHAMS V2: Failed to save config to files." << std::endl;
    }
}
