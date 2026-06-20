#include "menu_client.hpp"

#include <GLFW/glfw3.h>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "external/imgui/imgui.h"
#include "external/imgui/imgui_impl_glfw.h"
#include "external/imgui/imgui_impl_opengl3.h"
#include "logger.hpp"
#include "renderer/gl_loader.hpp"
#include "menu_theme.hpp"
#include "menu_tabs.hpp"

MenuClient::MenuClient(OverlayConfig& config) : cfg(config)
{
    init_window();
    init_imgui();
}

MenuClient::~MenuClient()
{
    glfwMakeContextCurrent(window);
    preview.cleanup();
    if (imgui_context) {
        ImGui::SetCurrentContext(imgui_context);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(imgui_context);
        imgui_context = nullptr;
    }
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
}

bool MenuClient::should_close() const
{
    return glfwWindowShouldClose(window);
}

void MenuClient::init_window()
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_FLOATING, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    width = cfg.menu_w;
    height = cfg.menu_h;
    window = glfwCreateWindow(width, height, "FC2 Chams V2 - Settings", nullptr, nullptr);
    if (!window) {
        throw std::runtime_error("MENU_CLIENT: Failed to create setting window");
    }

    // Position setting window near the center-left of the screen
    glfwSetWindowPos(window, 100, 100);
}

void MenuClient::init_imgui()
{
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);  // Disable V-Sync to prevent blocking the shared overlay main loop

    IMGUI_CHECKVERSION();
    imgui_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui_context);
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Load smallest pixel-7 font (scaled slightly larger for menu)
    ImFont* font = io.Fonts->AddFontFromFileTTF("assets/smallest_pixel-7.ttf", 10.0f);
    if (!font) {
        font = io.Fonts->AddFontFromFileTTF("../assets/smallest_pixel-7.ttf", 10.0f);
    }
    if (!font) {
        font = io.Fonts->AddFontFromFileTTF("/home/milo/Desktop/fc2-chams-rewrite/fc2-chams/assets/smallest_pixel-7.ttf", 10.0f);
    }
    if (!font) {
        FC2_LOG_ERROR("MENU_CLIENT: Failed to load smallest_pixel-7.ttf font, falling back to default.");
        io.Fonts->AddFontDefault();
    }

    apply_dark_theme();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
}

void MenuClient::render()
{
    // Track current GL context to skip redundant context switches
    if (glfwGetCurrentContext() != window) {
        glfwMakeContextCurrent(window);
    }

    ImGui::SetCurrentContext(imgui_context);

    int cur_w, cur_h;
    glfwGetWindowSize(window, &cur_w, &cur_h);
    if (cur_w != cfg.menu_w || cur_h != cfg.menu_h) {
        cfg.set_menu_w(cur_w);
        cfg.set_menu_h(cur_h);
        width = cur_w;
        height = cur_h;
    }

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

void MenuClient::render_ui()
{
    OverlayConfig old_cfg = cfg;
    preview.init(cfg);
    preview.render(cfg, preview_rotation);

    // Fill the window
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("FC2 Chams Settings Menu", nullptr, window_flags);

    // Title / Header Banner
    float title_width = ImGui::CalcTextSize("FC2-Chams Control Panel").x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - title_width) * 0.5f);
    ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.00f), "FC2-Chams Control Panel");
    ImGui::Separator();
    ImGui::Spacing();

    // Left column settings panel
    ImGui::BeginChild("LeftSettingsPanel", ImVec2(450.0f, 0.0f), false);

    if (ImGui::BeginTabBar("SettingsTabs")) {
        if (ImGui::BeginTabItem("Chams")) {
            render_chams_tab(cfg);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("ESP")) {
            render_esp_tab(cfg);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Trajectories")) {
            render_trajectories_tab(cfg);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("VPK Parser")) {
            render_vpk_tab(cfg);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Overlay Settings")) {
            render_overlay_tab(cfg, overlay_window, window);
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
        ImGui::Dummy(ImVec2(0.0f, 17.0f));  // keeps layout consistent
    }

    ImGui::Spacing();
    if (ImGui::Button("Save Configuration", ImVec2(-1.0f, 40.0f))) {
        save_config("overlay.json", cfg);
        save_msg_timer = 3.0f;  // Show save text for 3 seconds
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextDisabled("Close this window to terminate overlay process.");

    ImGui::EndChild();  // End LeftSettingsPanel

    ImGui::SameLine();

    // Right column preview panel
    ImGui::BeginChild("RightPreviewPanel", ImVec2(0.0f, 0.0f), false);
    float preview_title_width = ImGui::CalcTextSize("Visuals Preview").x;
    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - preview_title_width) * 0.5f);
    ImGui::TextColored(ImVec4(0.40f, 0.70f, 1.00f, 1.00f), "Visuals Preview");
    ImGui::Separator();
    ImGui::Spacing();

    if (preview.get_texture()) {
        ImGui::Image((void*)(intptr_t)preview.get_texture(), ImVec2((float)preview.get_width(), (float)preview.get_height()), ImVec2(0.0f, 1.0f),
                     ImVec2(1.0f, 0.0f));
    } else {
        ImGui::Dummy(ImVec2((float)preview.get_width(), (float)preview.get_height()));
    }

    ImGui::Spacing();
    ImGui::Text("Preview Rotation:");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##PreviewRotation", &preview_rotation, -180.0f, 180.0f, "%.1f deg");

    ImGui::EndChild();  // End RightPreviewPanel

    ImGui::End();

    // Compare config with the saved copy to mark dirty if any setting changed
    OverlayConfig temp_current = cfg;
    OverlayConfig temp_old = old_cfg;
    temp_current.clear_dirty();
    temp_old.clear_dirty();
    if (!(temp_current == temp_old)) {
        cfg.mark_dirty();
    }
}
