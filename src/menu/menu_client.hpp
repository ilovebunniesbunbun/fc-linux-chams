#pragma once
#include "renderer/gl_loader.hpp"
#include "config.hpp"
#include "menu_preview.hpp"

class MenuClient {
public:
    MenuClient(OverlayConfig& config);
    ~MenuClient();

    bool should_close() const;
    void render();

    GLFWwindow* get_window() const { return window; }
    void set_overlay_window(GLFWwindow* w) { overlay_window = w; }

private:
    void init_window();
    void init_imgui();
    void render_ui();

    MenuPreview preview;
    float preview_rotation = 0.0f; // rotation in degrees

    GLFWwindow* window = nullptr;
    GLFWwindow* overlay_window = nullptr;
    OverlayConfig& cfg;
    int width = 820;
    int height = 620;
};
