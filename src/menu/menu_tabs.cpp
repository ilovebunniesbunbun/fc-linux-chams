#include "menu_tabs.hpp"
#include "config.hpp"
#include "renderer/gl_loader.hpp"
#include "external/imgui/imgui.h"
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <utility>
#include <cstring>

// Style dropdown options used in chams tab
static const std::vector<std::pair<std::string, std::string>> style_options = {
    {"disabled", "Disabled"}, {"metallic", "Metallic (Fresnel)"}, {"flat", "Flat Color"},
    {"textured", "Textured"}, {"glow_blend", "Glow Blend"},       {"cs2_glow", "CS2 Glow"},
    {"wireframe", "Wireframe"}};

static void render_style_combo(const char* label, ChamsStyle& current_style) {
    std::string current_label = "Disabled";
    for (const auto& opt : style_options) {
        if (opt.first == (std::string)current_style) {
            current_label = opt.second;
            break;
        }
    }

    if (ImGui::BeginCombo(label, current_label.c_str())) {
        for (const auto& opt : style_options) {
            bool is_selected = (opt.first == (std::string)current_style);
            if (ImGui::Selectable(opt.second.c_str(), is_selected)) {
                current_style = ChamsStyle(opt.first);
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void render_chams_tab(OverlayConfig& cfg) {
    ImGui::Spacing();

    // Visible Chams Header
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Visible Chams");
    ImGui::Separator();
    render_style_combo("Visible Style", cfg.style_vis);

    if (cfg.style_vis != "disabled") {
        ImGui::ColorEdit4("Visible Body Color", cfg.color_vis, ImGuiColorEditFlags_AlphaBar);
        if (cfg.style_vis == "glow_blend" || cfg.style_vis == "cs2_glow") {
            ImGui::ColorEdit4("Visible Body Color 2", cfg.color_vis_sec, ImGuiColorEditFlags_AlphaBar);
        }
    }
    ImGui::Spacing();
    ImGui::Spacing();

    // Hidden Chams Header
    ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "Occluded Chams");
    ImGui::Separator();
    ImGui::Checkbox("Show Invisible (Behind Walls)", &cfg.show_invisible);

    if (cfg.show_invisible) {
        render_style_combo("Hidden Style", cfg.style_invis);
        if (cfg.style_invis != "disabled") {
            ImGui::ColorEdit4("Hidden Body Color", cfg.color_invis, ImGuiColorEditFlags_AlphaBar);
            if (cfg.style_invis == "glow_blend" || cfg.style_invis == "cs2_glow") {
                ImGui::ColorEdit4("Hidden Body Color 2", cfg.color_invis_sec, ImGuiColorEditFlags_AlphaBar);
            }
        }
    }
    ImGui::Spacing();

    if (cfg.style_vis == "flat" || (cfg.show_invisible && cfg.style_invis == "flat")) {
        ImGui::Checkbox("Uniform Flat Chams (No Overlap)", &cfg.flat_chams_no_overlap);
        ImGui::SetItemTooltip("Eliminates internal overlapping details when flat chams are semi-transparent.");
        ImGui::Spacing();
    }

    ImGui::Spacing();

    // Player Outlines Header
    ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.9f, 1.0f), "Player Outline");
    ImGui::Separator();
    ImGui::Checkbox("Enable Player Outline", &cfg.glow_enabled);

    bool has_glow_style =
        cfg.style_vis == "glow_blend" || cfg.style_vis == "cs2_glow" ||
        (cfg.show_invisible && (cfg.style_invis == "glow_blend" || cfg.style_invis == "cs2_glow"));

    if (cfg.glow_enabled) {
        ImGui::Spacing();
        ImGui::Text("Outline Type:");

        static const std::vector<std::pair<std::string, std::string>> outline_mode_options = {
            {"glow", "Glow Outline (Screen Space)"}, {"stencil", "Stencil Outline (Crisp Mesh)"}};

        std::string current_mode_label = "Glow Outline (Screen Space)";
        for (const auto& opt : outline_mode_options) {
            if (opt.first == cfg.outline_mode) {
                current_mode_label = opt.second;
                break;
            }
        }

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##OutlineType", current_mode_label.c_str())) {
            for (const auto& opt : outline_mode_options) {
                bool is_selected = (opt.first == cfg.outline_mode);
                if (ImGui::Selectable(opt.second.c_str(), is_selected)) {
                    cfg.outline_mode = opt.first;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    bool show_glow_controls = (cfg.glow_enabled && cfg.outline_mode == "glow") || has_glow_style;
    bool show_stencil_controls = (cfg.glow_enabled && cfg.outline_mode == "stencil") && !has_glow_style;

    if (show_glow_controls || show_stencil_controls) {
        ImGui::Spacing();

        if (show_glow_controls) {
            ImGui::Text("Glow Thickness: %.2f", cfg.glow_thickness);
        } else {
            ImGui::Text("Outline Thickness: %.2f", cfg.glow_thickness);
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##GlowThickness", &cfg.glow_thickness, 0.0f, 6.0f, "%.2f");

        if (cfg.glow_health_based) {
            ImGui::Text("Health Start Color (100 HP):");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::ColorEdit4("##HealthStartColor", cfg.glow_health_start, ImGuiColorEditFlags_AlphaBar);

            ImGui::Text("Health End Color (1 HP):");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::ColorEdit4("##HealthEndColor", cfg.glow_health_end, ImGuiColorEditFlags_AlphaBar);
        } else {
            if (show_glow_controls) {
                ImGui::Text("Glow Color:");
            } else {
                ImGui::Text("Outline Color:");
            }
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::ColorEdit4("##GlowColor", cfg.glow_color, ImGuiColorEditFlags_AlphaBar);
        }

        ImGui::Checkbox("Health-Based Color", &cfg.glow_health_based);

        if (show_glow_controls) {
            ImGui::Text("Glow Strength: %.2f", cfg.glow_intensity);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##GlowIntensity", &cfg.glow_intensity, 0.0f, 5.0f, "%.2f");

            ImGui::Spacing();
            ImGui::Checkbox("Enable Breathing/Pulse Effect", &cfg.glow_pulse);
            if (cfg.glow_pulse) {
                ImGui::Text("Pulse Speed: %.2f", cfg.glow_pulse_speed);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderFloat("##GlowPulseSpeed", &cfg.glow_pulse_speed, 0.1f, 10.0f, "%.2f");
            }
        }
        ImGui::Spacing();
    }
}

void render_esp_tab(OverlayConfig& cfg) {
    ImGui::Spacing();
    ImGui::Checkbox("Enable ESP Overlay", &cfg.esp_enabled);
    ImGui::Separator();

    if (cfg.esp_enabled) {
        // Skeleton Section
        ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.00f), "Skeleton");
        ImGui::Checkbox("Draw Skeleton##skele", &cfg.esp_skeleton);
        if (cfg.esp_skeleton) {
            ImGui::Checkbox("Rounded Skeleton (Curved)", &cfg.esp_rounded_skeleton);
            ImGui::SliderFloat("Skeleton Thickness##skele_th", &cfg.esp_skeleton_thickness, 0.5f, 5.0f, "%.1f");
            ImGui::ColorEdit4("Visible Color##skele_vis", cfg.esp_skeleton_color_vis, ImGuiColorEditFlags_AlphaBar);
            ImGui::ColorEdit4("Occluded Color##skele_invis", cfg.esp_skeleton_color_invis, ImGuiColorEditFlags_AlphaBar);
            ImGui::Checkbox("Skeleton glow##skele_glow", &cfg.esp_skeleton_glow);
            if (cfg.esp_skeleton_glow) {
                ImGui::Checkbox("Glow Pulse##skele_glow_puls", &cfg.esp_skeleton_glow_pulse);
                if (cfg.esp_skeleton_glow_pulse) {
                    ImGui::SliderFloat("Glow Pulse Speed##skele_glow_puls_spd", &cfg.esp_skeleton_glow_pulse_speed, 0.1f, 10.0f, "%.1f");
                }
                ImGui::SliderFloat("Glow Thickness##skele_glow_th", &cfg.esp_skeleton_glow_thickness, 0.1f, 10.0f, "%.1f");
                ImGui::SliderFloat("Glow Intensity##skele_glow_int", &cfg.esp_skeleton_glow_intensity, 0.1f, 5.0f, "%.1f");
                ImGui::Checkbox("Glow Health Based##skele_glow_hb", &cfg.esp_skeleton_glow_health_based);
                if (cfg.esp_skeleton_glow_health_based) {
                    ImGui::ColorEdit4("Glow Health Start##skele_glow_hs", cfg.esp_skeleton_glow_health_start, ImGuiColorEditFlags_AlphaBar);
                    ImGui::ColorEdit4("Glow Health End##skele_glow_he", cfg.esp_skeleton_glow_health_end, ImGuiColorEditFlags_AlphaBar);
                } else {
                    ImGui::ColorEdit4("Glow Color##skele_glow_col", cfg.esp_skeleton_glow_color, ImGuiColorEditFlags_AlphaBar);
                }
            }
        }
        ImGui::Spacing();
        ImGui::Separator();

        // Box Section
        ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.00f), "Bounding Box");
        ImGui::Checkbox("Draw Box##box", &cfg.esp_box);
        if (cfg.esp_box) {
            ImGui::SliderFloat("Box Thickness##box_th", &cfg.esp_box_thickness, 0.5f, 5.0f, "%.1f");
            ImGui::ColorEdit4("Box Color##box_col", cfg.esp_box_color, ImGuiColorEditFlags_AlphaBar);
            ImGui::Checkbox("Box Outline##box_outl", &cfg.esp_box_outline);
            const char* box_modes[] = {"Dynamic (Bones)", "Dynamic (Feet + Head)", "Static (Distance-Based)"};
            ImGui::Combo("Box Mode##box_mode", &cfg.esp_box_mode, box_modes, IM_ARRAYSIZE(box_modes));

            if (cfg.esp_box_mode == 2) {
                ImGui::SliderFloat("Static Box Width##box_stat_w", &cfg.esp_box_static_w, 5000.0f, 80000.0f, "%.0f");
                ImGui::SliderFloat("Static Box Height##box_stat_h", &cfg.esp_box_static_h, 5000.0f, 120000.0f, "%.0f");
            }
        }
        ImGui::Spacing();
        ImGui::Separator();

        // Health Bar Section
        ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.00f), "Health Bar");
        ImGui::Checkbox("Draw Health Bar##hp", &cfg.esp_health_bar);
        if (cfg.esp_health_bar) {
            ImGui::SliderFloat("Health Bar Thickness##hp_th", &cfg.esp_health_bar_thickness, 0.5f, 10.0f, "%.1f");
            ImGui::Checkbox("Gradient##hp_grad", &cfg.esp_health_bar_gradient);
            if (cfg.esp_health_bar_gradient) {
                ImGui::ColorEdit4("Gradient High Health##hp_gh", cfg.esp_health_bar_gradient_start, ImGuiColorEditFlags_AlphaBar);
                ImGui::ColorEdit4("Gradient Low Health##hp_gl", cfg.esp_health_bar_gradient_end, ImGuiColorEditFlags_AlphaBar);
            } else {
                ImGui::ColorEdit4("Static Health Color##hp_col", cfg.esp_health_bar_color, ImGuiColorEditFlags_AlphaBar);
            }
            ImGui::Checkbox("Health Bar Outline##hp_outl", &cfg.esp_health_bar_outline);
        }
    }
}

void render_trajectories_tab(OverlayConfig& cfg) {
    ImGui::Spacing();
    ImGui::Checkbox("Enable Grenade Trajectory", &cfg.draw_grenade_trajectory);
    ImGui::Checkbox("Show Through Walls", &cfg.trajectory_show_through_walls);
    ImGui::Separator();

    if (cfg.draw_grenade_trajectory) {
        ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.00f), "Colors");
        ImGui::ColorEdit4("Trail Color", cfg.grenade_trajectory_color, ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("Bounce Box Color", cfg.trajectory_bounce_color, ImGuiColorEditFlags_AlphaBar);
        ImGui::ColorEdit4("Detonation Circle Color", cfg.trajectory_detonation_color, ImGuiColorEditFlags_AlphaBar);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.00f), "Sizes & Timings");
        ImGui::SliderFloat("Line Thickness", &cfg.trajectory_thickness, 0.5f, 5.0f, "%.1f");
        ImGui::SliderFloat("Bounce Box Size", &cfg.trajectory_bounce_size, 0.5f, 10.0f, "%.1f");
        ImGui::SliderFloat("Detonation Radius", &cfg.trajectory_detonation_radius, 5.0f, 50.0f, "%.1f");
        ImGui::SliderFloat("Fade Duration", &cfg.trajectory_fade_time, 0.1f, 5.0f, "%.1fs");
    }
}

void render_vpk_tab(OverlayConfig& cfg) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "VPK Parser");
    ImGui::Separator();

    ImGui::Checkbox("Enable Vis-Check", &cfg.use_depth_prepass);
    ImGui::SetItemTooltip("Uploads and renders full VPK map geometry on the GPU to occlude player chams realistically.");

    ImGui::Spacing();
    ImGui::Text("Active Mode:");
    ImGui::SameLine();
    if (cfg.use_depth_prepass) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "GPU Depth Prepass");
    } else {
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "None (Always Visible)");
    }
    ImGui::Spacing();
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "Map Geometry");
    ImGui::Separator();
    ImGui::Checkbox("Show Map Geometry", &cfg.map_visualizer_enabled);
    if (cfg.map_visualizer_enabled) {
        ImGui::Checkbox("Only Visibile", &cfg.map_visualizer_depth_tested);
        ImGui::ColorEdit4("Wireframe Color", cfg.map_visualizer_color, ImGuiColorEditFlags_AlphaBar);
    }
    ImGui::Spacing();
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "Directory Paths");

    char maps_dir_buf[256];
    std::strncpy(maps_dir_buf, cfg.maps_dir.c_str(), sizeof(maps_dir_buf));
    if (ImGui::InputText("Maps Path", maps_dir_buf, sizeof(maps_dir_buf))) {
        cfg.maps_dir = maps_dir_buf;
    }

    char vpk_path_buf[256];
    std::strncpy(vpk_path_buf, cfg.vpk_path.c_str(), sizeof(vpk_path_buf));
    if (ImGui::InputText("VPK Path", vpk_path_buf, sizeof(vpk_path_buf))) {
        cfg.vpk_path = vpk_path_buf;
    }
}

void render_overlay_tab(OverlayConfig& cfg, GLFWwindow* overlay_window, GLFWwindow* menu_window) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Performance & Rendering");
    ImGui::Separator();

    ImGui::SliderInt("Target FPS", &cfg.fps, 0, 360, "%d FPS");
    ImGui::SetItemTooltip("0 means unlimited. The overlay uses a high-precision sleeper to match the rate.");

    ImGui::Checkbox("Show FPS Counter", &cfg.show_fps);

    bool prev_vsync = cfg.vsync;
    ImGui::Checkbox("Enable VSync", &cfg.vsync);
    ImGui::SetItemTooltip(
        "Synchronizes the overlay's swap interval to the monitor refresh rate.\nReduces tearing but may cap "
        "FPS. Overrides Target FPS when enabled.");
    if (cfg.vsync != prev_vsync && overlay_window) {
        // Apply VSync change on the overlay window context
        glfwMakeContextCurrent(overlay_window);
        glfwSwapInterval(cfg.vsync ? 1 : 0);
        glfwMakeContextCurrent(menu_window);
    }

    ImGui::Checkbox("Extrapolate View Matrix", &cfg.extrapolate);
    ImGui::SetItemTooltip("Performs linear extrapolation of game view projection matrix to smooth chams tracking.");

    ImGui::Checkbox("Hyprland Compatibility Mode", &cfg.hyprland_support);
    ImGui::SetItemTooltip(
        "Bypasses X11 override_redirect. Useful for wlroots-based WMs like Hyprland.\nNote: Requires "
        "restarting the overlay application to take effect.");

    ImGui::Checkbox("Debug Bridge Packets", &cfg.debug_bridge);
    ImGui::Checkbox("GPU Profiling", &cfg.debug_gpu_profiling);
    ImGui::SetItemTooltip("Toggles the logging of shared memory bridge packet stats in the terminal.");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Geometry & Scaling");
    ImGui::Separator();

    static const std::vector<std::pair<std::string, std::string>> scaling_options = {
        {"stretched", "Stretched (Full Screen)"},
        {"centered", "Centered (Black Bars)"},
        {"custom", "Custom Position / Size"}};

    std::string current_scaling_label = "Stretched (Full Screen)";
    for (const auto& opt : scaling_options) {
        if (opt.first == cfg.scaling) {
            current_scaling_label = opt.second;
            break;
        }
    }

    if (ImGui::BeginCombo("Scaling Mode", current_scaling_label.c_str())) {
        for (const auto& opt : scaling_options) {
            bool is_selected = (opt.first == cfg.scaling);
            if (ImGui::Selectable(opt.second.c_str(), is_selected)) {
                cfg.scaling = opt.first;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::InputInt("Monitor Width", &cfg.monitor_w);
    ImGui::InputInt("Monitor Height", &cfg.monitor_h);
    ImGui::InputInt("Game Width", &cfg.game_w);
    ImGui::InputInt("Game Height", &cfg.game_h);

    if (cfg.scaling == "custom") {
        ImGui::InputInt("Offset X (Game Left)", &cfg.game_x);
        ImGui::InputInt("Offset Y (Game Top)", &cfg.game_y);
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Hardware Selection");
    ImGui::Separator();

    // Display current GPU info
    const char* gl_vendor = (const char*)glGetString(GL_VENDOR);
    const char* gl_renderer = (const char*)glGetString(GL_RENDERER);
    if (gl_vendor && gl_renderer) {
        ImGui::Text("Active GPU: %s (%s)", gl_renderer, gl_vendor);
    }

    // GPU Preference combo dynamically loaded from system
    std::vector<GpuDevice> detected_gpus = detect_gpus();

    std::string current_gpu_label = "Default / System Decision";
    if (cfg.gpu_preference == "default") {
        current_gpu_label = "Default / System Decision";
    } else {
        for (const auto& gpu : detected_gpus) {
            if (gpu.name == cfg.gpu_preference) {
                current_gpu_label = gpu.display_name;
                break;
            }
        }
    }

    if (ImGui::BeginCombo("Preferred GPU", current_gpu_label.c_str())) {
        bool is_default_selected = (cfg.gpu_preference == "default");
        if (ImGui::Selectable("Default / System Decision", is_default_selected)) {
            cfg.gpu_preference = "default";
        }
        if (is_default_selected) {
            ImGui::SetItemDefaultFocus();
        }

        for (const auto& gpu : detected_gpus) {
            bool is_selected = (gpu.name == cfg.gpu_preference);
            if (ImGui::Selectable(gpu.display_name.c_str(), is_selected)) {
                cfg.gpu_preference = gpu.name;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip(
        "Forces OpenGL driver loading via Prime render offload settings.\n(Requires restarting the application "
        "to apply).");

    // Monitor Selection combo
    int monitor_count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitor_count);
    std::string current_monitor_label = "Monitor 0 (Primary)";

    if (monitors && monitor_count > 0) {
        int target_idx = cfg.monitor_index;
        if (target_idx < 0 || target_idx >= monitor_count) {
            target_idx = 0;
        }
        GLFWmonitor* m = monitors[target_idx];
        const GLFWvidmode* mode = glfwGetVideoMode(m);
        char label_buf[128];
        if (mode) {
            std::snprintf(label_buf, sizeof(label_buf), "Monitor %d (%s) - %dx%d", target_idx,
                          glfwGetMonitorName(m), mode->width, mode->height);
        } else {
            std::snprintf(label_buf, sizeof(label_buf), "Monitor %d (%s)", target_idx, glfwGetMonitorName(m));
        }
        current_monitor_label = label_buf;
    }

    if (ImGui::BeginCombo("Target Monitor", current_monitor_label.c_str())) {
        for (int i = 0; i < monitor_count; ++i) {
            GLFWmonitor* m = monitors[i];
            const GLFWvidmode* mode = glfwGetVideoMode(m);
            char label_buf[128];
            if (mode) {
                std::snprintf(label_buf, sizeof(label_buf), "Monitor %d (%s) - %dx%d", i, glfwGetMonitorName(m),
                              mode->width, mode->height);
            } else {
                std::snprintf(label_buf, sizeof(label_buf), "Monitor %d (%s)", i, glfwGetMonitorName(m));
            }

            bool is_selected = (i == cfg.monitor_index);
            if (ImGui::Selectable(label_buf, is_selected)) {
                cfg.monitor_index = i;
            }
            if (is_selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip(
        "Sets the monitor where the transparent game overlay will be placed.\n(Requires restarting the "
        "application to apply).");
    ImGui::Spacing();
}
