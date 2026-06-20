#pragma once
#include <cstring>
#include <nlohmann/json_fwd.hpp>
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

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;

    Color() = default;
    Color(float red, float green, float blue, float alpha) : r(red), g(green), b(blue), a(alpha)
    {
    }

    bool operator==(const Color& other) const
    {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    bool operator!=(const Color& other) const
    {
        return !(*this == other);
    }

    float& operator[](size_t index)
    {
        return (&r)[index];
    }
    const float& operator[](size_t index) const
    {
        return (&r)[index];
    }
    operator float*()
    {
        return &r;
    }
    operator const float*() const
    {
        return &r;
    }
    float* data()
    {
        return &r;
    }
    const float* data() const
    {
        return &r;
    }
};

enum class ChamsStyleEnum { DISABLED, METALLIC, FLAT, TEXTURED, GLOW_BLEND, CS2_GLOW, WIREFRAME };

class ChamsStyle {
public:
    ChamsStyleEnum value = ChamsStyleEnum::FLAT;

    ChamsStyle() = default;
    ChamsStyle(ChamsStyleEnum val) : value(val)
    {
    }
    ChamsStyle(const std::string& str)
    {
        if (str == "metallic")
            value = ChamsStyleEnum::METALLIC;
        else if (str == "flat")
            value = ChamsStyleEnum::FLAT;
        else if (str == "textured")
            value = ChamsStyleEnum::TEXTURED;
        else if (str == "glow_blend")
            value = ChamsStyleEnum::GLOW_BLEND;
        else if (str == "cs2_glow")
            value = ChamsStyleEnum::CS2_GLOW;
        else if (str == "wireframe")
            value = ChamsStyleEnum::WIREFRAME;
        else
            value = ChamsStyleEnum::DISABLED;
    }
    ChamsStyle(const char* str) : ChamsStyle(std::string(str))
    {
    }

    operator std::string() const
    {
        switch (value) {
            case ChamsStyleEnum::METALLIC:
                return "metallic";
            case ChamsStyleEnum::FLAT:
                return "flat";
            case ChamsStyleEnum::TEXTURED:
                return "textured";
            case ChamsStyleEnum::GLOW_BLEND:
                return "glow_blend";
            case ChamsStyleEnum::CS2_GLOW:
                return "cs2_glow";
            case ChamsStyleEnum::WIREFRAME:
                return "wireframe";
            default:
                return "disabled";
        }
    }

    bool operator==(const ChamsStyle& other) const
    {
        return value == other.value;
    }
    bool operator!=(const ChamsStyle& other) const
    {
        return value != other.value;
    }
    bool operator==(const std::string& str) const
    {
        return (std::string) * this == str;
    }
    bool operator!=(const std::string& str) const
    {
        return (std::string) * this != str;
    }
    bool operator==(const char* str) const
    {
        return (std::string) * this == str;
    }
    bool operator!=(const char* str) const
    {
        return (std::string) * this != str;
    }
};

// JSON serialization forward declarations
void to_json(nlohmann::json& j, const Color& color);
void from_json(const nlohmann::json& j, Color& color);
void to_json(nlohmann::json& j, const ChamsStyle& style);
void from_json(const nlohmann::json& j, ChamsStyle& style);

#define CONFIG_FIELDS                                                                                         \
    X(int, monitor_w, "monitor_w", 2560)                                                                      \
    X(int, monitor_h, "monitor_h", 1440)                                                                      \
    X(int, game_w, "game_w", 1280)                                                                            \
    X(int, game_h, "game_h", 960)                                                                             \
    X(std::string, scaling, "scaling", "")                                                                    \
    X(int, game_x, "game_x", 0)                                                                               \
    X(int, game_y, "game_y", 0)                                                                               \
    X(int, fps, "fps", 143)                                                                                   \
    X(bool, show_fps, "show_fps", true)                                                                       \
    X(bool, vsync, "vsync", false)                                                                            \
    X(bool, extrapolate, "extrapolate", false)                                                                \
    X(Color, color_vis, "color_visible", Color(0.0f, 1.0f, 0.0f, 200.0f / 255.0f))                            \
    X(Color, color_vis_sec, "color_visible_sec", Color(1.0f, 1.0f, 0.0f, 0.8f))                               \
    X(Color, color_invis, "color_invisible", Color(1.0f, 0.0f, 0.0f, 200.0f / 255.0f))                        \
    X(Color, color_invis_sec, "color_invisible_sec", Color(1.0f, 0.5f, 0.0f, 0.8f))                           \
    X(bool, show_invisible, "show_invisible", true)                                                           \
    X(std::string, maps_dir, "maps_dir", "./maps")                                                            \
    X(bool, hyprland_support, "hyprland_support", false)                                                      \
    X(int, monitor_index, "monitor_index", 0)                                                                 \
    X(std::string, gpu_preference, "gpu_preference", "default")                                               \
    X(std::string, vpk_path, "vpk_path", "auto")                                                              \
    X(ChamsStyle, style_vis, "chams_style_visible", ChamsStyle(ChamsStyleEnum::FLAT))                         \
    X(ChamsStyle, style_invis, "chams_style_hidden", ChamsStyle(ChamsStyleEnum::FLAT))                        \
    X(bool, use_depth_prepass, "use_depth_prepass", true)                                                     \
    X(bool, use_bvh_fallback, "use_bvh_fallback", true)                                                       \
    X(bool, flat_chams_no_overlap, "flat_chams_no_overlap", true)                                             \
    X(bool, glow_enabled, "glow_enabled", false)                                                              \
    X(std::string, outline_mode, "outline_mode", "glow")                                                      \
    X(bool, glow_health_based, "glow_health_based", false)                                                    \
    X(Color, glow_color, "glow_color", Color(1.0f, 0.0f, 1.0f, 0.8f))                                         \
    X(Color, glow_health_start, "glow_health_start", Color(0.0f, 1.0f, 0.0f, 0.8f))                           \
    X(Color, glow_health_end, "glow_health_end", Color(1.0f, 0.0f, 0.0f, 0.8f))                               \
    X(float, glow_thickness, "glow_thickness", 0.0f)                                                          \
    X(float, glow_intensity, "glow_intensity", 1.0f)                                                          \
    X(bool, glow_pulse, "glow_pulse", false)                                                                  \
    X(float, glow_pulse_speed, "glow_pulse_speed", 2.0f)                                                      \
    X(bool, esp_enabled, "esp_enabled", true)                                                                 \
    X(bool, esp_skeleton, "esp_skeleton", false)                                                              \
    X(bool, esp_rounded_skeleton, "esp_rounded_skeleton", false)                                              \
    X(float, esp_skeleton_thickness, "esp_skeleton_thickness", 1.5f)                                          \
    X(Color, esp_skeleton_color_vis, "esp_skeleton_color_vis", Color(0.0f, 1.0f, 0.0f, 0.8f))                 \
    X(Color, esp_skeleton_color_invis, "esp_skeleton_color_invis", Color(1.0f, 0.0f, 0.0f, 0.8f))             \
    X(bool, esp_skeleton_glow, "esp_skeleton_glow", false)                                                    \
    X(Color, esp_skeleton_glow_color, "esp_skeleton_glow_color", Color(0.0f, 1.0f, 1.0f, 0.8f))               \
    X(bool, esp_skeleton_glow_pulse, "esp_skeleton_glow_pulse", false)                                        \
    X(float, esp_skeleton_glow_pulse_speed, "esp_skeleton_glow_pulse_speed", 2.0f)                            \
    X(float, esp_skeleton_glow_intensity, "esp_skeleton_glow_intensity", 1.0f)                                \
    X(float, esp_skeleton_glow_thickness, "esp_skeleton_glow_thickness", 1.5f)                                \
    X(bool, esp_skeleton_glow_health_based, "esp_skeleton_glow_health_based", false)                          \
    X(Color, esp_skeleton_glow_health_start, "esp_skeleton_glow_health_start", Color(0.0f, 1.0f, 0.0f, 0.8f)) \
    X(Color, esp_skeleton_glow_health_end, "esp_skeleton_glow_health_end", Color(1.0f, 0.0f, 0.0f, 0.8f))     \
    X(bool, esp_box, "esp_box", false)                                                                        \
    X(float, esp_box_thickness, "esp_box_thickness", 1.5f)                                                    \
    X(Color, esp_box_color, "esp_box_color", Color(1.0f, 1.0f, 1.0f, 0.8f))                                   \
    X(bool, esp_box_outline, "esp_box_outline", true)                                                         \
    X(int, esp_box_mode, "esp_box_mode", 1)                                                                   \
    X(float, esp_box_static_w, "esp_box_static_w", 41500.0f)                                                  \
    X(float, esp_box_static_h, "esp_box_static_h", 76200.0f)                                                  \
    X(bool, esp_health_bar, "esp_health_bar", false)                                                          \
    X(bool, esp_health_bar_gradient, "esp_health_bar_gradient", true)                                         \
    X(Color, esp_health_bar_color, "esp_health_bar_color", Color(0.0f, 1.0f, 0.0f, 0.8f))                     \
    X(Color, esp_health_bar_gradient_start, "esp_health_bar_gradient_start", Color(0.0f, 1.0f, 0.0f, 0.8f))   \
    X(Color, esp_health_bar_gradient_end, "esp_health_bar_gradient_end", Color(1.0f, 0.0f, 0.0f, 0.8f))       \
    X(bool, esp_health_bar_outline, "esp_health_bar_outline", true)                                           \
    X(float, esp_health_bar_thickness, "esp_health_bar_thickness", 2.0f)                                      \
    X(bool, debug_bridge, "debug_bridge", false)                                                              \
    X(bool, debug_gpu_profiling, "debug_gpu_profiling", false)                                                \
    X(bool, map_visualizer_enabled, "map_visualizer_enabled", false)                                          \
    X(bool, map_visualizer_depth_tested, "map_visualizer_depth_tested", true)                                 \
    X(Color, map_visualizer_color, "map_visualizer_color", Color(0.0f, 0.784f, 0.392f, 0.47f))                \
    X(bool, draw_grenade_trajectory, "draw_grenade_trajectory", true)                                         \
    X(Color, grenade_trajectory_color, "grenade_trajectory_color", Color(0.67f, 0.69f, 0.86f, 0.8f))          \
    X(Color, trajectory_bounce_color, "trajectory_bounce_color", Color(0.76f, 0.78f, 0.84f, 1.0f))            \
    X(Color, trajectory_detonation_color, "trajectory_detonation_color", Color(0.55f, 0.59f, 0.92f, 1.0f))    \
    X(float, trajectory_thickness, "trajectory_thickness", 2.0f)                                              \
    X(float, trajectory_bounce_size, "trajectory_bounce_size", 2.0f)                                          \
    X(float, trajectory_detonation_radius, "trajectory_detonation_radius", 15.0f)                             \
    X(float, trajectory_fade_time, "trajectory_fade_time", 1.5f)                                              \
    X(bool, trajectory_show_through_walls, "trajectory_show_through_walls", false)                            \
    X(int, menu_w, "menu_w", 820)                                                                             \
    X(int, menu_h, "menu_h", 620)

struct OverlayConfig {
#define X(type, name, json_key, default_val) mutable type name = default_val;
    CONFIG_FIELDS
#undef X

    // Default equality operator (C++20)
    bool operator==(const OverlayConfig& other) const = default;

    // Dirty flag state
    mutable bool dirty_ = false;
    void mark_dirty() const
    {
        dirty_ = true;
    }
    bool is_dirty() const
    {
        return dirty_;
    }
    void clear_dirty() const
    {
        dirty_ = false;
    }

    // Setters kept inline for menu window sizing compatibility
    void set_menu_w(int val) const
    {
        if (menu_w != val) {
            menu_w = val;
            mark_dirty();
        }
    }
    void set_menu_h(int val) const
    {
        if (menu_h != val) {
            menu_h = val;
            mark_dirty();
        }
    }
};

OverlayConfig load_config(const std::string& filename);
int get_style_id(const std::string& name);
void save_config(const std::string& filename, const OverlayConfig& cfg);
bool validate_config(const std::string& filename, std::string& error_msg);
