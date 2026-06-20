#pragma once

#include "config.hpp"
#include "renderer/esp_renderer.hpp"

class GpuChamsRenderer;
class ModelCache;

class MenuPreview {
public:
    MenuPreview() = default;
    ~MenuPreview();

    void init(const OverlayConfig& cfg);
    void cleanup();
    void render(OverlayConfig& cfg, float rotation_deg);

    unsigned int get_texture() const { return preview_tex; }
    int get_width() const { return preview_w; }
    int get_height() const { return preview_h; }

private:
    bool preview_initialized = false;
    unsigned int preview_fbo = 0;
    unsigned int preview_tex = 0;
    unsigned int preview_depth = 0;
    int preview_w = 320;
    int preview_h = 450;

    GpuChamsRenderer* preview_renderer = nullptr;
    ModelCache* preview_model_cache = nullptr;
    EspRenderer esp_renderer;
};
