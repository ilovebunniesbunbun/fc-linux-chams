#pragma once
#include <GLFW/glfw3.h>
#include "config.hpp"

class GpuChamsRenderer;
class ModelCache;

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

    // Player Preview Resources
    bool preview_initialized = false;
    unsigned int preview_fbo = 0;
    unsigned int preview_tex = 0;
    unsigned int preview_depth = 0;
    int preview_w = 320;
    int preview_h = 450;
    GpuChamsRenderer* preview_renderer = nullptr;
    ModelCache* preview_model_cache = nullptr;
    float preview_rotation = 0.0f; // rotation in degrees

    void init_preview();
    void render_preview();
    void cleanup_preview();

    GLFWwindow* window = nullptr;
    OverlayConfig& cfg;
    int width = 820;
    int height = 620;
};
