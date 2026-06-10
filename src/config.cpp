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
                {"vsync", false},
                {"extrapolate", false},
                {"color_visible", {0, 255, 0, 166}},
                {"color_visible_sec", {255, 255, 0, 204}},
                {"color_invisible", {255, 0, 0, 166}},
                {"color_invisible_sec", {255, 127, 0, 204}},
                {"show_invisible", true},
                {"maps_dir", "./maps"},
                {"hyprland_support", false},
                {"vpk_path", "auto"},
                {"chams_style_visible", "metallic"},
                {"chams_style_hidden", "flat"},
                {"use_depth_prepass", true},
                {"use_bvh_fallback", true},
                {"glow_enabled", false},
                {"glow_health_based", false},
                {"glow_health_start", {0, 255, 0, 204}},
                {"glow_health_end", {255, 0, 0, 204}},
                {"glow_thickness", 1.5},
                {"glow_intensity", 1.0},
                {"glow_color", {255, 0, 255, 204}},
                {"glow_pulse", false},
                {"glow_pulse_speed", 2.0},
                {"esp_enabled", true},
                {"esp_skeleton", true},
                {"esp_rounded_skeleton", false},
                {"esp_skeleton_thickness", 1.5},
                {"esp_skeleton_color_vis", {0, 255, 0, 204}},
                {"esp_skeleton_color_invis", {255, 0, 0, 204}},
                {"esp_skeleton_glow", false},
                {"esp_skeleton_glow_color", {0, 255, 255, 204}},
                {"esp_skeleton_glow_pulse", false},
                {"esp_skeleton_glow_pulse_speed", 2.0},
                {"esp_skeleton_glow_intensity", 1.0},
                {"esp_skeleton_glow_thickness", 1.5},
                {"esp_skeleton_glow_health_based", false},
                {"esp_skeleton_glow_health_start", {0, 255, 0, 204}},
                {"esp_skeleton_glow_health_end", {255, 0, 0, 204}},
                {"esp_box", true},
                {"esp_box_thickness", 1.5},
                {"esp_box_color", {255, 255, 255, 204}},
                {"esp_box_outline", true},
                {"esp_box_mode", 1},
                {"esp_box_static_w", 41500.0},
                {"esp_box_static_h", 76200.0},
                {"esp_health_bar", true},
                {"esp_health_bar_gradient", true},
                {"esp_health_bar_color", {0, 255, 0, 204}},
                {"esp_health_bar_gradient_start", {0, 255, 0, 204}},
                {"esp_health_bar_gradient_end", {255, 0, 0, 204}},
                {"esp_health_bar_outline", true},
                {"esp_health_bar_thickness", 2.0},
                {"debug_bridge", false}
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
        if (j.contains("vsync")) cfg.vsync = j["vsync"].get<bool>();
        if (j.contains("extrapolate")) cfg.extrapolate = j["extrapolate"].get<bool>();
        if (j.contains("maps_dir")) cfg.maps_dir = j["maps_dir"].get<std::string>();
        if (j.contains("show_invisible")) cfg.show_invisible = j["show_invisible"].get<bool>();
        if (j.contains("hyprland_support")) cfg.hyprland_support = j["hyprland_support"].get<bool>();

        if (j.contains("color_visible") && j["color_visible"].is_array() && j["color_visible"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.color_vis[i] = j["color_visible"][i].get<float>() / 255.0f;
            }
        }
        if (j.contains("color_visible_sec") && j["color_visible_sec"].is_array() && j["color_visible_sec"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.color_vis_sec[i] = j["color_visible_sec"][i].get<float>() / 255.0f;
            }
        }
        if (j.contains("color_invisible") && j["color_invisible"].is_array() && j["color_invisible"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.color_invis[i] = j["color_invisible"][i].get<float>() / 255.0f;
            }
        }
        if (j.contains("color_invisible_sec") && j["color_invisible_sec"].is_array() && j["color_invisible_sec"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.color_invis_sec[i] = j["color_invisible_sec"][i].get<float>() / 255.0f;
            }
        }

        if (j.contains("vpk_path")) cfg.vpk_path = j["vpk_path"].get<std::string>();
        if (j.contains("chams_style_visible")) cfg.style_vis = j["chams_style_visible"].get<std::string>();
        if (j.contains("chams_style_hidden")) cfg.style_invis = j["chams_style_hidden"].get<std::string>();
        if (j.contains("use_depth_prepass")) cfg.use_depth_prepass = j["use_depth_prepass"].get<bool>();
        if (j.contains("use_bvh_fallback")) cfg.use_bvh_fallback = j["use_bvh_fallback"].get<bool>();
        if (j.contains("debug_bridge")) cfg.debug_bridge = j["debug_bridge"].get<bool>();

        if (j.contains("glow_enabled")) cfg.glow_enabled = j["glow_enabled"].get<bool>();
        if (j.contains("glow_health_based")) cfg.glow_health_based = j["glow_health_based"].get<bool>();
        if (j.contains("glow_thickness")) cfg.glow_thickness = j["glow_thickness"].get<float>();
        if (j.contains("glow_intensity")) cfg.glow_intensity = j["glow_intensity"].get<float>();
        if (j.contains("glow_pulse")) cfg.glow_pulse = j["glow_pulse"].get<bool>();
        if (j.contains("glow_pulse_speed")) cfg.glow_pulse_speed = j["glow_pulse_speed"].get<float>();

        if (j.contains("glow_color") && j["glow_color"].is_array() && j["glow_color"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.glow_color[i] = j["glow_color"][i].get<float>() / 255.0f;
            }
        }
        if (j.contains("glow_health_start") && j["glow_health_start"].is_array() && j["glow_health_start"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.glow_health_start[i] = j["glow_health_start"][i].get<float>() / 255.0f;
            }
        }
        if (j.contains("glow_health_end") && j["glow_health_end"].is_array() && j["glow_health_end"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.glow_health_end[i] = j["glow_health_end"][i].get<float>() / 255.0f;
            }
        }

        if (j.contains("esp_enabled")) cfg.esp_enabled = j["esp_enabled"].get<bool>();
        if (j.contains("esp_skeleton")) cfg.esp_skeleton = j["esp_skeleton"].get<bool>();
        if (j.contains("esp_rounded_skeleton")) cfg.esp_rounded_skeleton = j["esp_rounded_skeleton"].get<bool>();
        if (j.contains("esp_skeleton_thickness")) cfg.esp_skeleton_thickness = j["esp_skeleton_thickness"].get<float>();
        if (j.contains("esp_skeleton_glow")) cfg.esp_skeleton_glow = j["esp_skeleton_glow"].get<bool>();
        if (j.contains("esp_skeleton_glow_pulse")) cfg.esp_skeleton_glow_pulse = j["esp_skeleton_glow_pulse"].get<bool>();
        if (j.contains("esp_skeleton_glow_pulse_speed")) cfg.esp_skeleton_glow_pulse_speed = j["esp_skeleton_glow_pulse_speed"].get<float>();
        if (j.contains("esp_skeleton_glow_intensity")) cfg.esp_skeleton_glow_intensity = j["esp_skeleton_glow_intensity"].get<float>();
        if (j.contains("esp_skeleton_glow_thickness")) cfg.esp_skeleton_glow_thickness = j["esp_skeleton_glow_thickness"].get<float>();
        if (j.contains("esp_skeleton_glow_health_based")) cfg.esp_skeleton_glow_health_based = j["esp_skeleton_glow_health_based"].get<bool>();
        if (j.contains("esp_box")) cfg.esp_box = j["esp_box"].get<bool>();
        if (j.contains("esp_box_thickness")) cfg.esp_box_thickness = j["esp_box_thickness"].get<float>();
        if (j.contains("esp_box_outline")) cfg.esp_box_outline = j["esp_box_outline"].get<bool>();
        if (j.contains("esp_box_mode")) {
            cfg.esp_box_mode = j["esp_box_mode"].get<int>();
        } else if (j.contains("esp_box_static")) {
            cfg.esp_box_mode = j["esp_box_static"].get<bool>() ? 2 : 0;
        }
        if (j.contains("esp_box_static_w")) cfg.esp_box_static_w = j["esp_box_static_w"].get<float>();
        if (j.contains("esp_box_static_h")) cfg.esp_box_static_h = j["esp_box_static_h"].get<float>();
        if (j.contains("esp_health_bar")) cfg.esp_health_bar = j["esp_health_bar"].get<bool>();
        if (j.contains("esp_health_bar_gradient")) cfg.esp_health_bar_gradient = j["esp_health_bar_gradient"].get<bool>();
        if (j.contains("esp_health_bar_outline")) cfg.esp_health_bar_outline = j["esp_health_bar_outline"].get<bool>();
        if (j.contains("esp_health_bar_thickness")) cfg.esp_health_bar_thickness = j["esp_health_bar_thickness"].get<float>();

        if (j.contains("esp_skeleton_color_vis") && j["esp_skeleton_color_vis"].is_array() && j["esp_skeleton_color_vis"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.esp_skeleton_color_vis[i] = j["esp_skeleton_color_vis"][i].get<float>() / 255.0f;
            }
        }
        if (j.contains("esp_skeleton_color_invis") && j["esp_skeleton_color_invis"].is_array() && j["esp_skeleton_color_invis"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.esp_skeleton_color_invis[i] = j["esp_skeleton_color_invis"][i].get<float>() / 255.0f;
            }
        }
        if (j.contains("esp_skeleton_glow_color") && j["esp_skeleton_glow_color"].is_array() && j["esp_skeleton_glow_color"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.esp_skeleton_glow_color[i] = j["esp_skeleton_glow_color"][i].get<float>() / 255.0f;
            }
        }
        if (j.contains("esp_box_color") && j["esp_box_color"].is_array() && j["esp_box_color"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.esp_box_color[i] = j["esp_box_color"][i].get<float>() / 255.0f;
            }
        }
        if (j.contains("esp_health_bar_color") && j["esp_health_bar_color"].is_array() && j["esp_health_bar_color"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.esp_health_bar_color[i] = j["esp_health_bar_color"][i].get<float>() / 255.0f;
            }
        }
        if (j.contains("esp_health_bar_gradient_start") && j["esp_health_bar_gradient_start"].is_array() && j["esp_health_bar_gradient_start"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.esp_health_bar_gradient_start[i] = j["esp_health_bar_gradient_start"][i].get<float>() / 255.0f;
            }
        }
        if (j.contains("esp_health_bar_gradient_end") && j["esp_health_bar_gradient_end"].is_array() && j["esp_health_bar_gradient_end"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.esp_health_bar_gradient_end[i] = j["esp_health_bar_gradient_end"][i].get<float>() / 255.0f;
            }
        }
        if (j.contains("esp_skeleton_glow_health_start") && j["esp_skeleton_glow_health_start"].is_array() && j["esp_skeleton_glow_health_start"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.esp_skeleton_glow_health_start[i] = j["esp_skeleton_glow_health_start"][i].get<float>() / 255.0f;
            }
        }
        if (j.contains("esp_skeleton_glow_health_end") && j["esp_skeleton_glow_health_end"].is_array() && j["esp_skeleton_glow_health_end"].size() == 4) {
            for (int i = 0; i < 4; ++i) {
                cfg.esp_skeleton_glow_health_end[i] = j["esp_skeleton_glow_health_end"][i].get<float>() / 255.0f;
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
        {"vsync", cfg.vsync},
        {"extrapolate", cfg.extrapolate},
        {"color_visible", {
            static_cast<int>(cfg.color_vis[0] * 255.0f),
            static_cast<int>(cfg.color_vis[1] * 255.0f),
            static_cast<int>(cfg.color_vis[2] * 255.0f),
            static_cast<int>(cfg.color_vis[3] * 255.0f)
        }},
        {"color_visible_sec", {
            static_cast<int>(cfg.color_vis_sec[0] * 255.0f),
            static_cast<int>(cfg.color_vis_sec[1] * 255.0f),
            static_cast<int>(cfg.color_vis_sec[2] * 255.0f),
            static_cast<int>(cfg.color_vis_sec[3] * 255.0f)
        }},
        {"color_invisible", {
            static_cast<int>(cfg.color_invis[0] * 255.0f),
            static_cast<int>(cfg.color_invis[1] * 255.0f),
            static_cast<int>(cfg.color_invis[2] * 255.0f),
            static_cast<int>(cfg.color_invis[3] * 255.0f)
        }},
        {"color_invisible_sec", {
            static_cast<int>(cfg.color_invis_sec[0] * 255.0f),
            static_cast<int>(cfg.color_invis_sec[1] * 255.0f),
            static_cast<int>(cfg.color_invis_sec[2] * 255.0f),
            static_cast<int>(cfg.color_invis_sec[3] * 255.0f)
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
        {"glow_health_based", cfg.glow_health_based},
        {"glow_thickness", cfg.glow_thickness},
        {"glow_intensity", cfg.glow_intensity},
        {"glow_pulse", cfg.glow_pulse},
        {"glow_pulse_speed", cfg.glow_pulse_speed},
        {"glow_color", {
            static_cast<int>(cfg.glow_color[0] * 255.0f),
            static_cast<int>(cfg.glow_color[1] * 255.0f),
            static_cast<int>(cfg.glow_color[2] * 255.0f),
            static_cast<int>(cfg.glow_color[3] * 255.0f)
        }},
        {"glow_health_start", {
            static_cast<int>(cfg.glow_health_start[0] * 255.0f),
            static_cast<int>(cfg.glow_health_start[1] * 255.0f),
            static_cast<int>(cfg.glow_health_start[2] * 255.0f),
            static_cast<int>(cfg.glow_health_start[3] * 255.0f)
        }},
        {"glow_health_end", {
            static_cast<int>(cfg.glow_health_end[0] * 255.0f),
            static_cast<int>(cfg.glow_health_end[1] * 255.0f),
            static_cast<int>(cfg.glow_health_end[2] * 255.0f),
            static_cast<int>(cfg.glow_health_end[3] * 255.0f)
        }},
        {"esp_enabled", cfg.esp_enabled},
        {"esp_skeleton", cfg.esp_skeleton},
        {"esp_rounded_skeleton", cfg.esp_rounded_skeleton},
        {"esp_skeleton_thickness", cfg.esp_skeleton_thickness},
        {"esp_skeleton_glow", cfg.esp_skeleton_glow},
        {"esp_skeleton_glow_pulse", cfg.esp_skeleton_glow_pulse},
        {"esp_skeleton_glow_pulse_speed", cfg.esp_skeleton_glow_pulse_speed},
        {"esp_skeleton_glow_intensity", cfg.esp_skeleton_glow_intensity},
        {"esp_skeleton_glow_thickness", cfg.esp_skeleton_glow_thickness},
        {"esp_skeleton_glow_health_based", cfg.esp_skeleton_glow_health_based},
        {"esp_box", cfg.esp_box},
        {"esp_box_thickness", cfg.esp_box_thickness},
        {"esp_box_outline", cfg.esp_box_outline},
        {"esp_box_mode", cfg.esp_box_mode},
        {"esp_box_static", cfg.esp_box_mode == 2},
        {"esp_box_static_w", cfg.esp_box_static_w},
        {"esp_box_static_h", cfg.esp_box_static_h},
        {"esp_health_bar", cfg.esp_health_bar},
        {"esp_health_bar_gradient", cfg.esp_health_bar_gradient},
        {"esp_health_bar_outline", cfg.esp_health_bar_outline},
        {"esp_health_bar_thickness", cfg.esp_health_bar_thickness},
        {"esp_skeleton_color_vis", {
            static_cast<int>(cfg.esp_skeleton_color_vis[0] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_color_vis[1] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_color_vis[2] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_color_vis[3] * 255.0f)
        }},
        {"esp_skeleton_color_invis", {
            static_cast<int>(cfg.esp_skeleton_color_invis[0] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_color_invis[1] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_color_invis[2] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_color_invis[3] * 255.0f)
        }},
        {"esp_skeleton_glow_color", {
            static_cast<int>(cfg.esp_skeleton_glow_color[0] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_glow_color[1] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_glow_color[2] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_glow_color[3] * 255.0f)
        }},
        {"esp_skeleton_glow_health_start", {
            static_cast<int>(cfg.esp_skeleton_glow_health_start[0] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_glow_health_start[1] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_glow_health_start[2] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_glow_health_start[3] * 255.0f)
        }},
        {"esp_skeleton_glow_health_end", {
            static_cast<int>(cfg.esp_skeleton_glow_health_end[0] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_glow_health_end[1] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_glow_health_end[2] * 255.0f),
            static_cast<int>(cfg.esp_skeleton_glow_health_end[3] * 255.0f)
        }},
        {"esp_box_color", {
            static_cast<int>(cfg.esp_box_color[0] * 255.0f),
            static_cast<int>(cfg.esp_box_color[1] * 255.0f),
            static_cast<int>(cfg.esp_box_color[2] * 255.0f),
            static_cast<int>(cfg.esp_box_color[3] * 255.0f)
        }},
        {"esp_health_bar_color", {
            static_cast<int>(cfg.esp_health_bar_color[0] * 255.0f),
            static_cast<int>(cfg.esp_health_bar_color[1] * 255.0f),
            static_cast<int>(cfg.esp_health_bar_color[2] * 255.0f),
            static_cast<int>(cfg.esp_health_bar_color[3] * 255.0f)
        }},
        {"esp_health_bar_gradient_start", {
            static_cast<int>(cfg.esp_health_bar_gradient_start[0] * 255.0f),
            static_cast<int>(cfg.esp_health_bar_gradient_start[1] * 255.0f),
            static_cast<int>(cfg.esp_health_bar_gradient_start[2] * 255.0f),
            static_cast<int>(cfg.esp_health_bar_gradient_start[3] * 255.0f)
        }},
        {"esp_health_bar_gradient_end", {
            static_cast<int>(cfg.esp_health_bar_gradient_end[0] * 255.0f),
            static_cast<int>(cfg.esp_health_bar_gradient_end[1] * 255.0f),
            static_cast<int>(cfg.esp_health_bar_gradient_end[2] * 255.0f),
            static_cast<int>(cfg.esp_health_bar_gradient_end[3] * 255.0f)
        }},
        {"debug_bridge", cfg.debug_bridge}
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
