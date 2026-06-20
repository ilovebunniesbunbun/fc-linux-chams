#pragma once

#include "app/GrenadeTracker.hpp"
#include "renderer/esp_renderer.hpp"
#include "overlay/overlay_client.hpp"
#include "config.hpp"
#include "app/SvgCache.hpp"

class GrenadeRenderer {
public:
    void render(GrenadeTracker& tracker, EspRenderer& esp_renderer, const VischeckResult& render_result, const float* gl_vp, const float* view_matrix, const OverlayConfig& cfg, OverlayClient* overlay, const SvgCache& svg_cache);

private:
    bool was_held_trajectory_visible = false;
    uint8_t last_held_grenade_type = 0;
};
