#include "GrenadeHelperRenderer.hpp"
#include "overlay/esp_drawing.hpp"
#include "external/imgui/imgui.h"
#include <GL/glew.h>
#include <cmath>
#include <algorithm>

namespace {
    // Static configuration constants for layout and distance checking
    constexpr float MAX_VERTICAL_DISTANCE = 80.0f;          // Ignore lineups on different levels/floors
    constexpr float FLOATING_BADGE_Y_OFFSET = 40.0f;        // Offset of the floating badge above the lineup point
    constexpr float CIRCLE_ACTIVATION_DISTANCE = 37.5f;     // Radius at which the precise ground standing circle is drawn
    constexpr float GRENADE_STAND_CIRCLE_RADIUS = 1.025f;   // Physical radius of the player standing spot circle
    constexpr float MIN_TRANSITION_RANGE = 50.0f;           // Minimum transition distance range for UI fades/animations
    constexpr float BADGE_RADIUS = 18.0f;                  // Radius of the floating lineup badge

    inline ImU32 imcol32_from_color(const Color& c, float alpha_factor = 1.0f) {
        return IM_COL32(
            static_cast<int>(c.r * 255.0f),
            static_cast<int>(c.g * 255.0f),
            static_cast<int>(c.b * 255.0f),
            static_cast<int>(c.a * 255.0f * alpha_factor)
        );
    }

    inline Color get_grenade_color(uint8_t type, const OverlayConfig& cfg) {
        switch (type) {
            case GRENADE_HE: return cfg.grenade_color_he;
            case GRENADE_FLASH: return cfg.grenade_color_flash;
            case GRENADE_SMOKE: return cfg.grenade_color_smoke;
            case GRENADE_MOLOTOV: return cfg.grenade_color_molotov;
            case GRENADE_DECOY: return cfg.grenade_color_decoy;
            default: return Color(0.67f, 0.69f, 0.86f, 0.8f);
        }
    }

    void draw_sleek_badge(ImDrawList* draw_list, float sx, float sy, uint8_t grenade_type, const std::string& title, ImU32 badge_color, float fade_alpha, const SvgCache& svg_cache, float progress) {
        float radius = BADGE_RADIUS;
        
        ImU32 bg_col = IM_COL32(30, 30, 35, static_cast<int>(200.0f * fade_alpha));
        ImU32 radial_bg_col = IM_COL32(15, 15, 20, static_cast<int>(200.0f * fade_alpha));
        
        uint32_t badge_a = (badge_color >> 24) & 0xFF;
        ImU32 arc_col = (badge_color & 0x00FFFFFF) | (static_cast<uint32_t>(badge_a * fade_alpha) << 24);

        float square_width = radius * 2.0f;
        float rect_width = square_width;

        ImVec2 text_size(0.0f, 0.0f);
        float padding_left = 4.0f;
        float padding_right = 8.0f;

        if (!title.empty()) {
            text_size = ImGui::CalcTextSize(title.c_str());
            rect_width = radius * 2.0f + padding_left + text_size.x + padding_right;
        }

        float box_width = square_width + (rect_width - square_width) * progress;

        float left = sx - radius;
        float right = sx - radius + box_width;
        float top = sy - radius;
        float bottom = sy + radius;
        
        draw_list->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), bg_col, 0.0f);
        draw_list->AddRectFilled(ImVec2(left + 2.0f, top + 2.0f), ImVec2(right - 2.0f, bottom - 2.0f), radial_bg_col, 0.0f);
        
        SvgTexture tex = svg_cache.get_texture(grenade_type);
        if (tex.id != 0) {
            float max_dim = BADGE_RADIUS;
            float scale = max_dim / std::max(tex.width, tex.height);
            float icon_w = tex.width * scale;
            float icon_h = tex.height * scale;

            ImVec2 icon_min(sx - icon_w * 0.5f, sy - icon_h * 0.5f);
            ImVec2 icon_max(sx + icon_w * 0.5f, sy + icon_h * 0.5f);
            draw_list->AddImage((ImTextureID)(intptr_t)tex.id, icon_min, icon_max, ImVec2(0,0), ImVec2(1,1), IM_COL32(255,255,255, static_cast<int>(255.0f * fade_alpha)));
        }
        
        // Halved outline thickness = 1.0f
        draw_list->AddRect(ImVec2(left, top), ImVec2(right, bottom), arc_col, 0.0f, 0, 1.0f);
        
        if (progress >= 0.99f && !title.empty()) {
            ImVec2 text_pos(sx + radius + padding_left, sy - text_size.y * 0.5f);
            ImU32 text_shadow_col = IM_COL32(0, 0, 0, static_cast<int>(255.0f * fade_alpha));
            ImU32 text_col = IM_COL32(255, 255, 255, static_cast<int>(255.0f * fade_alpha));
            draw_list->AddText(ImVec2(text_pos.x + 1.0f, text_pos.y + 1.0f), text_shadow_col, title.c_str());
            draw_list->AddText(text_pos, text_col, title.c_str());
        }
    }

    void draw_world_circle(ImDrawList* draw_list, const Vec3& center, float radius, ImU32 color, float thickness, const float* view_matrix, float screen_w, float screen_h, bool filled) {
        const int num_segments = 32;
        std::vector<ImVec2> points;
        points.reserve(num_segments);
        for (int i = 0; i < num_segments; ++i) {
            float angle = i * (2.0f * 3.14159265f) / num_segments;
            Vec3 world_pt = {
                center.x + radius * std::cos(angle),
                center.y + radius * std::sin(angle),
                center.z
            };
            float sx = 0.0f, sy = 0.0f;
            if (world_to_screen(world_pt, &sx, &sy, view_matrix, screen_w, screen_h)) {
                points.push_back(ImVec2(sx, sy));
            }
        }
        if (points.size() >= 3) {
            if (filled) {
                ImU32 fill_color = (color & 0x00FFFFFF) | 0x20000000;
                draw_list->AddConvexPolyFilled(points.data(), points.size(), fill_color);
            }
            draw_list->AddPolyline(points.data(), points.size(), color, ImDrawFlags_Closed, thickness);
        }
    }
}

void GrenadeHelperRenderer::render(const std::vector<GrenadeLineup>& lineups, 
                                   const Vec3& local_pos,
                                   const Vec3& local_eye,
                                   const Vec3& /*local_angles*/,
                                   uint16_t local_weapon_id,
                                   const float* view_matrix,
                                   const OverlayConfig& cfg,
                                   OverlayClient* overlay,
                                   const SvgCache& svg_cache) {
    // 1. Safety checks: Ensure the feature is enabled and ImGui contexts/overlay structures are valid.
    if (!cfg.grenade_helper_enabled || !overlay || !overlay->get_imgui_context()) return;

    // 2. Identify the active grenade held by the local player based on current weapon ID.
    uint8_t target_grenade_type = GRENADE_NONE;
    switch (local_weapon_id) {
        case 44: target_grenade_type = GRENADE_HE; break;         // weapon_hegrenade
        case 43: target_grenade_type = GRENADE_FLASH; break;      // weapon_flashbang
        case 45: target_grenade_type = GRENADE_SMOKE; break;      // weapon_smokegrenade
        case 46: // weapon_molotov
        case 48: target_grenade_type = GRENADE_MOLOTOV; break;    // weapon_incgrenade
        case 47: target_grenade_type = GRENADE_DECOY; break;      // weapon_decoy
    }

    // Only process and render lineups if the player is holding a valid grenade.
    if (target_grenade_type == GRENADE_NONE) {
        return;
    }

    ImGui::SetCurrentContext(overlay->get_imgui_context());
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    float screen_w = overlay->get_width();
    float screen_h = overlay->get_height();

    // Iterate through all known lineups on the map.
    for (const auto& lineup : lineups) {
        // Filter out lineups that don't match the currently equipped grenade type.
        if (lineup.weapon != target_grenade_type) {
            continue;
        }

        // Calculate 2D distance (horizontal flat plane) and vertical distance (Z elevation) separately.
        Vec3 diff = { lineup.position.x - local_pos.x, lineup.position.y - local_pos.y, lineup.position.z - local_pos.z };
        float dist_2d = std::sqrt(diff.x * diff.x + diff.y * diff.y);
        float dist_z = std::abs(diff.z);

        // Distance Filters:
        // Ignore lineups that are too far away horizontally.
        if (dist_2d > cfg.grenade_helper_render_distance) continue;
        // Ignore lineups on different levels/floors (vertical tolerance offset).
        if (dist_z > MAX_VERTICAL_DISTANCE) continue; 

        // Smoothly fade out the lineup marker as it approaches the maximum render distance.
        float fade_alpha = 1.0f;
        float fade_start = cfg.grenade_helper_render_distance * 0.7f;
        if (dist_2d > fade_start) {
            fade_alpha = 1.0f - ((dist_2d - fade_start) / (cfg.grenade_helper_render_distance - fade_start));
        }
        fade_alpha = std::clamp(fade_alpha, 0.0f, 1.0f);

        Color g_col = get_grenade_color(lineup.weapon, cfg);
        ImU32 badge_color = imcol32_from_color(g_col, fade_alpha);

        // Calculate progression for expanding details on the floating badge when getting closer.
        float rect_start_dist = cfg.grenade_helper_render_distance * 0.5f;
        float transition_range = cfg.grenade_helper_render_distance * 0.15f;
        if (transition_range < MIN_TRANSITION_RANGE) transition_range = MIN_TRANSITION_RANGE;
        
        float progress = 0.0f;
        if (dist_2d <= rect_start_dist) {
            progress = (rect_start_dist - dist_2d) / transition_range;
            progress = std::clamp(progress, 0.0f, 1.0f);
        }

        // 3. Draw the Ground Position/Marker
        float sx = 0.0f, sy = 0.0f;
        if (world_to_screen(lineup.position, &sx, &sy, view_matrix, screen_w, screen_h)) {
            float floating_y = sy - FLOATING_BADGE_Y_OFFSET;
            float circle_activation_dist = CIRCLE_ACTIVATION_DISTANCE;
            float circle_radius = GRENADE_STAND_CIRCLE_RADIUS;
            float fade_out_dist = circle_activation_dist * 0.5f;

            // Precise standing circle triggers when the player gets extremely close to the center spot.
            if (dist_2d <= circle_activation_dist) {
                // Check if player is perfectly aligned inside the throwing spot.
                bool is_inside = (dist_2d <= cfg.grenade_helper_aim_radius);
                ImU32 ground_circle_col = is_inside ? IM_COL32(46, 204, 113, static_cast<int>(255.0f * fade_alpha)) : badge_color;
                draw_world_circle(draw_list, lineup.position, circle_radius, ground_circle_col, 1.0f, view_matrix, screen_w, screen_h, is_inside);
            }

            // Fade out the floating helper badge as the player gets close enough to see the precise circle.
            float badge_fade = 1.0f;
            if (dist_2d <= circle_activation_dist) {
                if (dist_2d <= fade_out_dist) {
                    badge_fade = 0.0f;
                } else {
                    badge_fade = (dist_2d - fade_out_dist) / (circle_activation_dist - fade_out_dist);
                }
            }

            // Draw the floating lineup emblem if it is within visibility.
            if (badge_fade > 0.001f) {
                draw_sleek_badge(draw_list, sx, floating_y, lineup.weapon, lineup.title, imcol32_from_color(g_col, 1.0f), fade_alpha * badge_fade, svg_cache, progress);
            }
        }

        // 4. Draw the Aim Target HUD Elements (Triggered when player is standing inside the aim radius)
        if (dist_2d <= cfg.grenade_helper_aim_radius) {
            // Convert lineup pitch/yaw to a 3D forward direction vector.
            float pitch = lineup.viewangles.x * (3.14159265f / 180.0f);
            float yaw = lineup.viewangles.y * (3.14159265f / 180.0f);

            Vec3 forward;
            forward.x = std::cos(pitch) * std::cos(yaw);
            forward.y = std::cos(pitch) * std::sin(yaw);
            forward.z = -std::sin(pitch); // Pitch direction convention in Source engine (-Z for down)

            // Cast a target point along the look vector.
            Vec3 aim_point;
            aim_point.x = local_eye.x + forward.x * 2000.0f;
            aim_point.y = local_eye.y + forward.y * 2000.0f;
            aim_point.z = local_eye.z + forward.z * 2000.0f;

            float ax = 0.0f, ay = 0.0f;
            if (world_to_screen(aim_point, &ax, &ay, view_matrix, screen_w, screen_h)) {
                // Draw target spot rings (aim crosshair).
                draw_list->AddCircleFilled(ImVec2(ax, ay), 3.0f, badge_color, 16);
                draw_list->AddCircle(ImVec2(ax, ay), 8.0f, badge_color, 16, 1.0f);

                // Calculate dimensions for the description and throwing instructions tooltip box.
                ImVec2 desc_size = ImGui::CalcTextSize(lineup.description.c_str());
                ImVec2 throw_size = ImGui::CalcTextSize(lineup.throw_method.c_str());
                float text_w = 0.0f;
                float text_h = 0.0f;

                if (!lineup.description.empty()) {
                    text_w = desc_size.x;
                    text_h += desc_size.y;
                }
                if (!lineup.throw_method.empty()) {
                    text_w = std::max(text_w, throw_size.x);
                    if (text_h > 0.0f) text_h += 4.0f; // Line separation padding
                    text_h += throw_size.y;
                }

                // Render the info popup near the aim target.
                if (text_w > 0.0f) {
                    float pad_x = 8.0f;
                    float pad_y = 6.0f;
                    
                    float top_y = ay + 18.0f;
                    float bottom_y = top_y + text_h + 2.0f * pad_y;
                    float left_x = ax - text_w * 0.5f - pad_x;
                    float right_x = ax + text_w * 0.5f + pad_x;

                    ImU32 bg_col = IM_COL32(30, 30, 35, static_cast<int>(245.0f * fade_alpha));
                    ImU32 border_col = IM_COL32(15, 15, 18, static_cast<int>(255.0f * fade_alpha));

                    // Info box background & borders
                    draw_list->AddRectFilled(ImVec2(left_x, top_y), ImVec2(right_x, bottom_y), bg_col, 0.0f);
                    draw_list->AddRect(ImVec2(left_x, top_y), ImVec2(right_x, bottom_y), border_col, 0.0f, 0, 1.0f);

                    float current_y = top_y + pad_y;
                    if (!lineup.description.empty()) {
                        ImVec2 pos1(ax - desc_size.x * 0.5f, current_y);
                        draw_list->AddText(pos1, IM_COL32(220, 220, 220, static_cast<int>(255.0f * fade_alpha)), lineup.description.c_str());
                        current_y += desc_size.y + 4.0f;
                    }
                    if (!lineup.throw_method.empty()) {
                        ImVec2 pos2(ax - throw_size.x * 0.5f, current_y);
                        draw_list->AddText(pos2, IM_COL32(255, 255, 255, static_cast<int>(255.0f * fade_alpha)), lineup.throw_method.c_str());
                    }
                }

                // Draw a guide line connecting screen center (player crosshair) to target aim location.
                float cx = screen_w * 0.5f;
                float cy = screen_h * 0.5f;
                
                float crosshair_dist = std::sqrt((ax - cx)*(ax - cx) + (ay - cy)*(ay - cy));
                float line_alpha = std::clamp(crosshair_dist / 100.0f, 0.0f, 1.0f);
                
                ImU32 line_col = imcol32_from_color(g_col, line_alpha * 0.5f * fade_alpha);
                if (line_alpha > 0.01f) {
                    draw_list->AddLine(ImVec2(cx, cy), ImVec2(ax, ay), line_col, 1.0f);
                }
            }
        }
    }
}
