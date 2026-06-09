#include "menu_client.hpp"
#include "external/imgui/imgui.h"
#include "external/imgui/imgui_impl_glfw.h"
#include "external/imgui/imgui_impl_opengl3.h"
#include "renderer/gpu_chams.hpp"
#include "model_cache.hpp"
#include "renderer/gl_loader.hpp"
#include <iostream>
#include <cstring>
#include <vector>
#include <utility>
#include <cmath>

MenuClient::MenuClient(OverlayConfig& config) : cfg(config) {
    init_window();
    init_imgui();
}

MenuClient::~MenuClient() {
    glfwMakeContextCurrent(window);
    cleanup_preview();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
}

bool MenuClient::should_close() const {
    return glfwWindowShouldClose(window);
}

void MenuClient::init_window() {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(width, height, "FC2 Chams V2 - Settings", nullptr, nullptr);
    if (!window) {
        throw std::runtime_error("MENU_CLIENT: Failed to create setting window");
    }

    // Position setting window near the center-left of the screen
    glfwSetWindowPos(window, 100, 100);
}

void MenuClient::init_imgui() {
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // Disable V-Sync to prevent blocking the shared overlay main loop

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    apply_dark_theme();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
}

void MenuClient::apply_dark_theme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.PopupRounding = 6.0f;

    colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_Border]                 = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.24f, 0.25f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.20f, 0.21f, 0.23f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.16f, 0.16f, 0.19f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.28f, 0.30f, 0.34f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.42f, 0.46f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.30f, 0.32f, 0.36f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.28f, 0.56f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.28f, 0.56f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.40f, 0.66f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.40f, 0.75f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.28f, 0.56f, 0.95f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.16f, 0.44f, 0.80f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.28f, 0.30f, 0.34f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.24f, 0.26f, 0.28f, 1.00f);
    colors[ImGuiCol_Separator]              = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.28f, 0.56f, 0.95f, 1.00f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.40f, 0.66f, 1.00f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.28f, 0.30f, 0.34f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.40f, 0.42f, 0.46f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.30f, 0.32f, 0.36f, 1.00f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.28f, 0.30f, 0.34f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.22f, 0.24f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);
}

void MenuClient::render() {
    glfwMakeContextCurrent(window);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    render_ui();

    ImGui::Render();

    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    
    // Smooth grey-black background
    glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}

void MenuClient::render_ui() {
    if (!preview_initialized) {
        init_preview();
    }
    render_preview();

    // Fill the window
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    
    ImGui::Begin("FC2 Chams Settings Menu", nullptr, window_flags);

    // Title / Header Banner
    ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.00f), "FC2 CHAMS V2 CONTROL PANEL");
    ImGui::Separator();
    ImGui::Spacing();

    // Left column settings panel
    ImGui::BeginChild("LeftSettingsPanel", ImVec2(450.0f, 0.0f), false);

    // Style dropdown options
    static const std::vector<std::pair<std::string, std::string>> style_options = {
        {"disabled", "Disabled"},
        {"metallic", "Metallic (Fresnel)"},
        {"flat", "Flat Color"},
        {"textured", "Textured"},
        {"glow_blend", "Glow Blend"},
        {"cs2_glow", "CS2 Glow"}
    };

    auto render_style_combo = [](const char* label, std::string& current_style) {
        std::string current_label = "Disabled";
        for (const auto& opt : style_options) {
            if (opt.first == current_style) {
                current_label = opt.second;
                break;
            }
        }

        if (ImGui::BeginCombo(label, current_label.c_str())) {
            for (const auto& opt : style_options) {
                bool is_selected = (opt.first == current_style);
                if (ImGui::Selectable(opt.second.c_str(), is_selected)) {
                    current_style = opt.first;
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    };

    if (ImGui::BeginTabBar("SettingsTabs")) {
        
        // ----------------- TAB 1: PLAYER CHAMS -----------------
        if (ImGui::BeginTabItem("Chams Styling")) {
            ImGui::Spacing();
            
            // Visible Chams Header
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Visible Chams (Models in View)");
            ImGui::Separator();
            render_style_combo("Visible Style", cfg.style_vis);
            
            if (cfg.style_vis != "disabled") {
                ImGui::ColorEdit4("Visible Body Color", cfg.color_vis, ImGuiColorEditFlags_AlphaBar);
            }
            ImGui::Spacing();
            ImGui::Spacing();

            // Hidden Chams Header
            ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "Hidden Chams (Models Occluded)");
            ImGui::Separator();
            ImGui::Checkbox("Show Invisible (Behind Walls)", &cfg.show_invisible);
            
            if (cfg.show_invisible) {
                render_style_combo("Hidden Style", cfg.style_invis);
                if (cfg.style_invis != "disabled") {
                    ImGui::ColorEdit4("Hidden Body Color", cfg.color_invis, ImGuiColorEditFlags_AlphaBar);
                }
            }
            ImGui::Spacing();
            ImGui::Spacing();

            // Outline Glow Header
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.9f, 1.0f), "Outline Glow (Always Visible)");
            ImGui::Separator();
            ImGui::Checkbox("Enable Outline Glow", &cfg.glow_enabled);
            
            if (cfg.glow_enabled) {
                ImGui::Spacing();

                if (cfg.glow_health_based) {
                    ImGui::Text("Health Start Color (100 HP):");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::ColorEdit4("##HealthStartColor", cfg.glow_health_start, ImGuiColorEditFlags_AlphaBar);

                    ImGui::Text("Health End Color (1 HP):");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::ColorEdit4("##HealthEndColor", cfg.glow_health_end, ImGuiColorEditFlags_AlphaBar);
                } else {
                    ImGui::Text("Glow Color:");
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::ColorEdit4("##GlowColor", cfg.glow_color, ImGuiColorEditFlags_AlphaBar);
                }

                ImGui::Checkbox("Health-Based Color", &cfg.glow_health_based);
                ImGui::SetItemTooltip("Interpolates glow color from Health Start Color (100 HP) to Health End Color (1 HP).");

                ImGui::Text("Glow Thickness: %.2f", cfg.glow_thickness);
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderFloat("##GlowThickness", &cfg.glow_thickness, 0.0f, 6.0f, "%.2f");

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
                ImGui::Spacing();
            }

            ImGui::EndTabItem();
        }

        // ----------------- TAB 2: OCCLUSION & ENGINE -----------------
        if (ImGui::BeginTabItem("Occlusion & Engine")) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "Depth Occlusion Handlers");
            ImGui::Separator();
            
            ImGui::Checkbox("Use Depth-Prepass (GPU)", &cfg.use_depth_prepass);
            ImGui::SetItemTooltip("Uploads and renders full VPK map geometry on the GPU to occlude player chams realistically.");

            ImGui::Checkbox("Use BVH Fallback (CPU)", &cfg.use_bvh_fallback);
            ImGui::SetItemTooltip("Calculates joint visibility on the CPU using raytracing when map geometry isn't loaded on GPU.");

            ImGui::Spacing();
            ImGui::Text("Active Mode:");
            ImGui::SameLine();
            if (cfg.use_depth_prepass) {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "GPU Depth Prepass");
            } else if (cfg.use_bvh_fallback) {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.8f, 1.0f), "CPU BVH Raytrace Fallback");
            } else {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "None (Always Visible)");
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

            ImGui::EndTabItem();
        }

        // ----------------- TAB 3: OVERLAY & SCREEN -----------------
        if (ImGui::BeginTabItem("Overlay Settings")) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Performance & Rendering");
            ImGui::Separator();
            
            ImGui::SliderInt("Target FPS", &cfg.fps, 0, 360, "%d FPS");
            ImGui::SetItemTooltip("0 means unlimited. The overlay uses a high-precision sleeper to match the rate.");
            
            ImGui::Checkbox("Show FPS Counter", &cfg.show_fps);
            ImGui::Checkbox("Extrapolate View Matrix", &cfg.extrapolate);
            ImGui::SetItemTooltip("Performs linear extrapolation of game view projection matrix to smooth chams tracking.");
            
            ImGui::Checkbox("Hyprland Compatibility Mode", &cfg.hyprland_support);
            ImGui::SetItemTooltip("Bypasses X11 override_redirect. Useful for wlroots-based WMs like Hyprland.\nNote: Requires restarting the overlay application to take effect.");

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Geometry & Scaling");
            ImGui::Separator();

            static const std::vector<std::pair<std::string, std::string>> scaling_options = {
                {"stretched", "Stretched (Full Screen)"},
                {"centered", "Centered (Black Bars)"},
                {"custom", "Custom Position / Size"}
            };

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

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Configuration file operations
    static float save_msg_timer = 0.0f;
    if (save_msg_timer > 0.0f) {
        save_msg_timer -= ImGui::GetIO().DeltaTime;
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Config saved to overlay.json successfully!");
    } else {
        ImGui::Dummy(ImVec2(0.0f, 17.0f)); // keeps layout consistent
    }

    ImGui::Spacing();
    if (ImGui::Button("Save Configuration", ImVec2(-1.0f, 40.0f))) {
        save_config("overlay.json", cfg);
        save_msg_timer = 3.0f; // Show save text for 3 seconds
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("Close this window to terminate overlay process.");

    ImGui::EndChild(); // End LeftSettingsPanel

    ImGui::SameLine();

    // Right column preview panel
    ImGui::BeginChild("RightPreviewPanel", ImVec2(0.0f, 0.0f), false);
    ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.00f), "Chams Style Preview");
    ImGui::Separator();
    ImGui::Spacing();

    if (preview_tex) {
        ImGui::Image((void*)(intptr_t)preview_tex, ImVec2((float)preview_w, (float)preview_h), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    } else {
        ImGui::Dummy(ImVec2((float)preview_w, (float)preview_h));
    }

    ImGui::Spacing();
    ImGui::Text("Preview Rotation:");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##PreviewRotation", &preview_rotation, -180.0f, 180.0f, "%.1f deg");

    ImGui::EndChild(); // End RightPreviewPanel

    ImGui::End();
}

// Player Preview Implementations
struct Vec3d {
    float x, y, z;
};

static inline Vec3d normalize_vec3d(Vec3d v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 0.00001f) {
        return { v.x / len, v.y / len, v.z / len };
    }
    return { 0, 0, 0 };
}

static inline Vec3d cross_vec3d(Vec3d a, Vec3d b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static void make_look_at(Vec3d eye, Vec3d center, Vec3d up, float* m) {
    Vec3d f = normalize_vec3d({ center.x - eye.x, center.y - eye.y, center.z - eye.z });
    Vec3d u = normalize_vec3d(up);
    Vec3d s = normalize_vec3d(cross_vec3d(f, u));
    u = cross_vec3d(s, f);

    m[0] = s.x;  m[4] = s.y;  m[8] = s.z;  m[12] = -(s.x * eye.x + s.y * eye.y + s.z * eye.z);
    m[1] = u.x;  m[5] = u.y;  m[9] = u.z;  m[13] = -(u.x * eye.x + u.y * eye.y + u.z * eye.z);
    m[2] = -f.x; m[6] = -f.y; m[10] = -f.z; m[14] = (f.x * eye.x + f.y * eye.y + f.z * eye.z);
    m[3] = 0.0f; m[7] = 0.0f; m[11] = 0.0f; m[15] = 1.0f;
}

static void make_perspective(float fovy, float aspect, float zNear, float zFar, float* m) {
    float rad = fovy * 3.14159265f / 180.0f;
    float tanHalfFovy = std::tan(rad / 2.0f);

    std::memset(m, 0, sizeof(float) * 16);
    m[0] = 1.0f / (aspect * tanHalfFovy);
    m[5] = 1.0f / tanHalfFovy;
    m[10] = -(zFar + zNear) / (zFar - zNear);
    m[11] = -1.0f;
    m[14] = -(2.0f * zFar * zNear) / (zFar - zNear);
}

static void multiply_matrices(const float* a, const float* b, float* r) {
    float temp[16];
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            temp[col * 4 + row] = sum;
        }
    }
    std::memcpy(r, temp, sizeof(float) * 16);
}

void MenuClient::init_preview() {
    if (preview_initialized) return;

    // Set VPK path override
    AgentParser::SetCustomVpkPath(cfg.vpk_path);

    // Create FBO
    glGenFramebuffers(1, &preview_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, preview_fbo);

    // Create color texture
    glGenTextures(1, &preview_tex);
    glBindTexture(GL_TEXTURE_2D, preview_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, preview_w, preview_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, preview_tex, 0);

    // Create depth renderbuffer
    glGenRenderbuffers(1, &preview_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, preview_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, preview_w, preview_h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, preview_depth);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "MENU_CLIENT: Failed to create preview framebuffer" << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Initialize sub-renderer and model cache
    preview_renderer = new GpuChamsRenderer();
    if (!preview_renderer->init()) {
        std::cerr << "MENU_CLIENT: Failed to initialize preview chams renderer" << std::endl;
    }

    preview_model_cache = new ModelCache();

    preview_initialized = true;
}

void MenuClient::cleanup_preview() {
    if (!preview_initialized) return;

    if (preview_fbo) {
        glDeleteFramebuffers(1, &preview_fbo);
        preview_fbo = 0;
    }
    if (preview_tex) {
        glDeleteTextures(1, &preview_tex);
        preview_tex = 0;
    }
    if (preview_depth) {
        glDeleteRenderbuffers(1, &preview_depth);
        preview_depth = 0;
    }

    if (preview_renderer) {
        delete preview_renderer;
        preview_renderer = nullptr;
    }

    if (preview_model_cache) {
        delete preview_model_cache;
        preview_model_cache = nullptr;
    }

    preview_initialized = false;
}

void MenuClient::render_preview() {
    if (!preview_initialized) return;

    // Synchronize VPK path override
    AgentParser::SetCustomVpkPath(cfg.vpk_path);

    const CachedModel* model = preview_model_cache->get_or_load("agents/models/ctm_sas/ctm_sas.vmdl");
    if (!model || !model->valid) {
        glBindFramebuffer(GL_FRAMEBUFFER, preview_fbo);
        glViewport(0, 0, preview_w, preview_h);
        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, preview_fbo);
    glViewport(0, 0, preview_w, preview_h);

    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float rad_angle = preview_rotation * 3.14159265f / 180.0f;
    float dist = 105.0f;
    Vec3d eye = { dist * std::cos(rad_angle), dist * std::sin(rad_angle), 39.2f };
    Vec3d center = { 0.0f, 0.0f, 39.2f };
    Vec3d up = { 0.0f, 0.0f, 1.0f };

    float view[16];
    make_look_at(eye, center, up, view);

    float proj[16];
    make_perspective(45.0f, (float)preview_w / preview_h, 0.1f, 1000.0f, proj);

    float view_proj[16];
    multiply_matrices(proj, view, view_proj);

    float gl_vp[16];
    std::memcpy(gl_vp, view_proj, sizeof(float) * 16);

    float cam_pos[3] = { eye.x, eye.y, eye.z };

    std::vector<source2::Mat3x4> identity_bones(model->mesh.bone_count);
    for (int j = 0; j < model->mesh.bone_count; ++j) {
        auto& b = identity_bones[j];
        std::memset(&b, 0, sizeof(b));
        b.mat[0][0] = 1.0f;
        b.mat[1][1] = 1.0f;
        b.mat[2][2] = 1.0f;
    }

    int style_vis_id = get_style_id(cfg.style_vis);

    if (cfg.glow_enabled) {
        preview_renderer->begin_glow_pass(preview_w, preview_h, gl_vp, cam_pos);
        preview_renderer->render_glow_silhouette(model->vao, model->ibo, model->index_count,
                                                 identity_bones, cfg.glow_color);

        float current_intensity = cfg.glow_intensity;
        if (cfg.glow_pulse) {
            float time = static_cast<float>(glfwGetTime());
            float factor = 0.625f + 0.375f * std::sin(time * cfg.glow_pulse_speed);
            current_intensity *= factor;
        }
        preview_renderer->end_glow_pass(preview_w, preview_h, cfg.glow_thickness, current_intensity, preview_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, preview_fbo);
    }

    if (style_vis_id > 0) {
        preview_renderer->begin_body_pass(gl_vp, cam_pos);
        preview_renderer->render_mesh(model->vao, model->ibo, model->index_count,
                                     identity_bones, cfg.color_vis, style_vis_id,
                                     nullptr, 0.0f, 0.0f);
        preview_renderer->end_body_pass();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
