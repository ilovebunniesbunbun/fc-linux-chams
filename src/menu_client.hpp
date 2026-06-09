#pragma once
#include <GLFW/glfw3.h>
#include "config.hpp"

class MenuClient {
public:
    MenuClient(OverlayConfig& config);
    ~MenuClient();

    bool should_close() const;
    void render();

    GLFWwindow* get_window() const { return window; }

private:
    void init_window();
    void init_imgui();
    void apply_dark_theme();
    void render_ui();

    GLFWwindow* window = nullptr;
    OverlayConfig& cfg;
    int width = 480;
    int height = 700;
};
