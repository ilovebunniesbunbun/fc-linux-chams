#include "GrenadeRenderer.hpp"
#include "logger.hpp"
#include "overlay/esp_drawing.hpp"
#include "external/imgui/imgui.h"
#include <GL/glew.h>
#include <algorithm>
#include <GLFW/glfw3.h>
#include <cstdio>

namespace {
    inline float cross_2d(const ImVec2& o, const ImVec2& a, const ImVec2& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    }

    std::vector<ImVec2> get_convex_hull(std::vector<ImVec2> pts) {
        if (pts.size() < 3) return pts;
        std::sort(pts.begin(), pts.end(), [](const ImVec2& a, const ImVec2& b) {
            if (a.x != b.x) return a.x < b.x;
            return a.y < b.y;
        });
        std::vector<ImVec2> lower;
        for (const auto& p : pts) {
            while (lower.size() >= 2 && cross_2d(lower[lower.size() - 2], lower.back(), p) <= 0.0f) {
                lower.pop_back();
            }
            lower.push_back(p);
        }
        std::vector<ImVec2> upper;
        for (auto it = pts.rbegin(); it != pts.rend(); ++it) {
            const auto& p = *it;
            while (upper.size() >= 2 && cross_2d(upper[upper.size() - 2], upper.back(), p) <= 0.0f) {
                upper.pop_back();
            }
            upper.push_back(p);
        }
        if (!lower.empty()) lower.pop_back();
        if (!upper.empty()) upper.pop_back();
        std::vector<ImVec2> hull;
        hull.reserve(lower.size() + upper.size());
        hull.insert(hull.end(), lower.begin(), lower.end());
        hull.insert(hull.end(), upper.begin(), upper.end());
        return hull;
    }

    inline ImU32 imcol32_from_color(const Color& c, float alpha_factor = 1.0f) {
        return IM_COL32(
            static_cast<int>(c.r * 255.0f),
            static_cast<int>(c.g * 255.0f),
            static_cast<int>(c.b * 255.0f),
            static_cast<int>(c.a * 255.0f * alpha_factor)
        );
    }

    inline ImU32 lerp_color(const Color& low, const Color& high, float frac) {
        float r = low.r + (high.r - low.r) * frac;
        float g = low.g + (high.g - low.g) * frac;
        float b = low.b + (high.b - low.b) * frac;
        float a = low.a + (high.a - low.a) * frac;
        return IM_COL32(
            static_cast<int>(r * 255.0f),
            static_cast<int>(g * 255.0f),
            static_cast<int>(b * 255.0f),
            static_cast<int>(a * 255.0f)
        );
    }

    inline Color get_grenade_color(uint8_t type, const OverlayConfig& cfg) {
        switch (type) {
            case GRENADE_HE: return cfg.grenade_color_he;
            case GRENADE_FLASH: return cfg.grenade_color_flash;
            case GRENADE_SMOKE: return cfg.grenade_color_smoke;
            case GRENADE_MOLOTOV: return cfg.grenade_color_molotov;
            case GRENADE_DECOY: return cfg.grenade_color_decoy;
            default: return cfg.grenade_trajectory_color;
        }
    }

    inline const char* get_grenade_name(uint8_t type) {
        switch (type) {
            case GRENADE_HE: return "HE";
            case GRENADE_FLASH: return "FLASH";
            case GRENADE_SMOKE: return "SMOKE";
            case GRENADE_MOLOTOV: return "FIRE";
            case GRENADE_DECOY: return "DECOY";
            default: return "GRENADE";
        }
    }

    void draw_badge_with_timer(ImDrawList* draw_list, float sx, float sy, uint8_t grenade_type, float remaining, float max_duration, ImU32 badge_color, float fade_alpha, const SvgCache& svg_cache) {
        float radius = 22.0f;
        float radial_radius = radius - 3.0f;
        
        // Background circle
        ImU32 bg_col = IM_COL32(30, 30, 35, static_cast<int>(200.0f * fade_alpha));
        draw_list->AddCircleFilled(ImVec2(sx, sy), radius, bg_col, 32);
        
        // Radial backdrop circle
        ImU32 radial_bg_col = IM_COL32(15, 15, 20, static_cast<int>(200.0f * fade_alpha));
        draw_list->AddCircleFilled(ImVec2(sx, sy), radial_radius, radial_bg_col, 32);
        
        // Draw SVG icon
        SvgTexture tex = svg_cache.get_texture(grenade_type);
        if (tex.id != 0) {
            float max_dim = 20.0f;
            float scale = max_dim / std::max(tex.width, tex.height);
            float icon_w = tex.width * scale;
            float icon_h = tex.height * scale;

            ImVec2 icon_min(sx - icon_w * 0.5f, sy - icon_h * 0.5f - 1.0f); // offset up slightly
            ImVec2 icon_max(sx + icon_w * 0.5f, sy + icon_h * 0.5f - 1.0f);
            draw_list->AddImage((ImTextureID)(intptr_t)tex.id, icon_min, icon_max, ImVec2(0,0), ImVec2(1,1), IM_COL32(255,255,255, static_cast<int>(255.0f * fade_alpha)));
        }

        // Timer radial progress
        if (max_duration > 0.0f) {
            float frac = std::clamp(remaining / max_duration, 0.0f, 1.0f);
            
            // Adjust badge color alpha for the radial line
            uint32_t badge_a = (badge_color >> 24) & 0xFF;
            ImU32 arc_col = (badge_color & 0x00FFFFFF) | (static_cast<uint32_t>(badge_a * fade_alpha) << 24);

            float start_angle = -3.14159265f / 2.0f;
            float end_angle = start_angle + (frac * 3.14159265f * 2.0f);
            
            // Fallback for full circle because PathArcTo struggles with 360 deg
            if (frac >= 0.999f) {
                draw_list->AddCircle(ImVec2(sx, sy), radial_radius, arc_col, 32, 2.5f);
            } else if (frac > 0.001f) {
                draw_list->PathArcTo(ImVec2(sx, sy), radial_radius, start_angle, end_angle, 32);
                draw_list->PathStroke(arc_col, 0, 2.5f);
            }
        }

        // Draw timer text under the circle
        if (max_duration > 0.0f) {
            char text_buf[32];
            std::snprintf(text_buf, sizeof(text_buf), "%.1fs", remaining);
            ImVec2 text_size = ImGui::CalcTextSize(text_buf);
            ImVec2 text_pos(sx - text_size.x * 0.5f, sy + radius + 4.0f);
            
            ImU32 text_shadow_col = IM_COL32(0, 0, 0, static_cast<int>(255.0f * fade_alpha));
            ImU32 text_col = IM_COL32(255, 255, 255, static_cast<int>(255.0f * fade_alpha));
            draw_list->AddText(ImVec2(text_pos.x + 1.0f, text_pos.y + 1.0f), text_shadow_col, text_buf);
            draw_list->AddText(text_pos, text_col, text_buf);
        }
    }

    void draw_impact_warning(ImDrawList* draw_list, float sx, float sy, int count, float fade_alpha = 1.0f) {
        char text_buf[64];
        std::snprintf(text_buf, sizeof(text_buf), "%d ENEMY(S)", count);
        
        ImVec2 text_size = ImGui::CalcTextSize(text_buf);
        float pad_x = 6.0f;
        float pad_y = 3.0f;
        
        ImVec2 rect_min(sx - text_size.x * 0.5f - pad_x, sy - text_size.y * 0.5f - pad_y);
        ImVec2 rect_max(sx + text_size.x * 0.5f + pad_x, sy + text_size.y * 0.5f + pad_y);
        
        ImU32 bg_col = IM_COL32(30, 30, 35, static_cast<int>(245.0f * fade_alpha));
        draw_list->AddRectFilled(rect_min, rect_max, bg_col, 0.0f);
        
        ImVec2 text_pos(sx - text_size.x * 0.5f, sy - text_size.y * 0.5f);
        ImU32 text_shadow_col = IM_COL32(0, 0, 0, static_cast<int>(255.0f * fade_alpha));
        ImU32 text_col = IM_COL32(255, 255, 255, static_cast<int>(255.0f * fade_alpha));
        draw_list->AddText(ImVec2(text_pos.x + 1.0f, text_pos.y + 1.0f), text_shadow_col, text_buf);
        draw_list->AddText(text_pos, text_col, text_buf);
    }
}

void GrenadeRenderer::render(GrenadeTracker& tracker, EspRenderer& esp_renderer, const VischeckResult& render_result, const float* gl_vp, const float* view_matrix, const OverlayConfig& cfg, OverlayClient* overlay, const SvgCache& svg_cache) {
    if (!cfg.draw_grenade_trajectory) return;

    esp_renderer.clear();
    esp_renderer.set_projection(gl_vp);

    if (cfg.trajectory_show_through_walls) {
        glDisable(GL_DEPTH_TEST);
    } else {
        glEnable(GL_DEPTH_TEST);
    }
    glDepthMask(GL_FALSE);

    // B) Draw Held Grenade Trajectory
    bool is_held_visible = render_result.held_trajectory.valid && !render_result.held_trajectory.points.empty();
    if (is_held_visible) {
        const auto& traj = render_result.held_trajectory;
        if (!was_held_trajectory_visible || traj.type != last_held_grenade_type) {
            FC2_LOG_INFO("[DEBUG-VISIBILITY] Held Grenade Trajectory went VISIBLE. "
                         "Reason: Player started aiming/holding a throw. "
                         "Type: {} ({}), Points: {}, Start Pos: ({:.2f}, {:.2f}, {:.2f}), End Pos: ({:.2f}, {:.2f}, {:.2f}), Affected enemies: {}",
                         traj.type, get_grenade_name(traj.type), traj.points.size(),
                         traj.points.empty() ? 0.0f : traj.points.front().x,
                         traj.points.empty() ? 0.0f : traj.points.front().y,
                         traj.points.empty() ? 0.0f : traj.points.front().z,
                         traj.end_pos.x, traj.end_pos.y, traj.end_pos.z,
                         traj.affected_count);
        }
        was_held_trajectory_visible = true;
        last_held_grenade_type = traj.type;

        Color g_col = get_grenade_color(traj.type, cfg);
        float color_arr[4] = { g_col.r, g_col.g, g_col.b, g_col.a };
        
        for (size_t i = 0; i < traj.points.size() - 1; ++i) {
            esp_renderer.add_line_3d(traj.points[i], traj.points[i + 1], color_arr, cfg.trajectory_thickness);
        }
        for (const auto& bounce : traj.bounces) {
            esp_renderer.add_box_instance(bounce, cfg.trajectory_bounce_size, color_arr);
        }
        esp_renderer.add_circle_instance(traj.end_pos, cfg.trajectory_detonation_radius, color_arr);

        // Render predicted affected enemies count at detonation point
        if (traj.affected_count > 0 && overlay && overlay->get_imgui_context()) {
            float dsx = 0.0f, dsy = 0.0f;
            if (world_to_screen(traj.end_pos, &dsx, &dsy, view_matrix, overlay->get_width(), overlay->get_height())) {
                ImGui::SetCurrentContext(overlay->get_imgui_context());
                ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
                draw_impact_warning(draw_list, dsx, dsy + 15.0f, traj.affected_count);
            }
        }
    } else {
        if (was_held_trajectory_visible) {
            was_held_trajectory_visible = false;
            last_held_grenade_type = 0;
        }
    }

    // C) Draw In-Flight Grenade Trajectories
    auto& tracked_active_trajectories = tracker.get_tracked_active_trajectories();
    if (!tracked_active_trajectories.empty()) {
        for (auto& tracked : tracked_active_trajectories) {
            if (!tracked.still_active) continue;
            const auto& traj = tracked.traj;
            if (!traj.valid || traj.points.empty()) continue;

            Color g_col = get_grenade_color(traj.type, cfg);
            float color_arr[4] = { g_col.r, g_col.g, g_col.b, g_col.a };

            if (!tracked.uploaded) {
                esp_renderer.upload_trajectory(tracked.entity_handle, tracked.spawn_time, traj.points, color_arr);
                tracked.uploaded = true;
            }

            esp_renderer.draw_gpu_trajectory(tracked.entity_handle, tracked.spawn_time, 0.0f, 1.0f, cfg.trajectory_thickness);

            // Draw bounce boxes
            for (const auto& bounce : traj.bounces) {
                esp_renderer.add_box_instance(bounce, cfg.trajectory_bounce_size, color_arr);
            }
            esp_renderer.add_circle_instance(traj.end_pos, cfg.trajectory_detonation_radius, color_arr);
        }
    }

    // D) Draw Fading/Erasing Grenade Trajectories
    auto& fading_trajectories = tracker.get_fading_trajectories();
    if (!fading_trajectories.empty()) {
        for (auto& fading : fading_trajectories) {
            const auto& traj = fading.traj;
            if (traj.points.empty()) continue;

            Color g_col = get_grenade_color(traj.type, cfg);

            // Calculate point where erasure has reached
            float float_idx = static_cast<float>(traj.points.size() - 1) * fading.erase_progress;
            size_t start_idx = static_cast<size_t>(float_idx);
            if (start_idx >= traj.points.size() - 1) continue;

            // Modulate colors by fade_alpha
            float color_mod[4] = {
                g_col.r,
                g_col.g,
                g_col.b,
                g_col.a * fading.fade_alpha
            };

            esp_renderer.draw_gpu_trajectory(traj.entity_handle, traj.spawn_time, fading.erase_progress, fading.fade_alpha, cfg.trajectory_thickness);

            // Draw remaining bounce boxes
            for (size_t b_idx = 0; b_idx < traj.bounces.size(); ++b_idx) {
                const auto& bounce = traj.bounces[b_idx];
                size_t closest_pt_idx = (b_idx < traj.bounce_indices.size()) ? traj.bounce_indices[b_idx] : 0;
                if (closest_pt_idx >= start_idx) {
                    esp_renderer.add_box_instance(bounce, cfg.trajectory_bounce_size, color_mod);
                }
            }

            // Draw detonation circle if it hasn't been erased yet
            if (start_idx < traj.points.size() - 1) {
                esp_renderer.add_circle_instance(traj.end_pos, cfg.trajectory_detonation_radius, color_mod);
            }
        }
    }

    esp_renderer.flush_lines();
    esp_renderer.flush_instances();
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    // E) Draw Warnings, Badges, Timers, and Inferno Polygons via ImGui context
    auto& tracked_infernos = tracker.get_tracked_infernos();
    auto& tracked_warnings = tracker.get_tracked_warnings();

    if (overlay && overlay->get_imgui_context()) {
        ImGui::SetCurrentContext(overlay->get_imgui_context());
        ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
        double current_real_time = glfwGetTime();

        // 1. Render ground fire (C_Inferno) convex hulls
        for (int i = 0; i < render_result.inferno_count; ++i) {
            const auto& inf_data = render_result.infernos[i];
            if (!inf_data.active || inf_data.fire_count <= 0) continue;

            float remaining = 0.0f;
            for (const auto& inf : tracked_infernos) {
                if (inf.entity_handle == inf_data.entity_handle) {
                    double elapsed = current_real_time - inf.first_seen_time;
                    remaining = std::max(0.0f, inf.duration - static_cast<float>(elapsed));
                    break;
                }
            }

            std::vector<ImVec2> screen_points;
            screen_points.reserve(inf_data.fire_count * 8);
            for (int j = 0; j < inf_data.fire_count; ++j) {
                const auto& fp = inf_data.fire_positions[j];
                for (int s = 0; s < 8; ++s) {
                    float angle = (static_cast<float>(s) / 8.0f) * 3.14159265f * 2.0f;
                    Vec3 wx(
                        fp.x + std::cos(angle) * 45.0f,
                        fp.y + std::sin(angle) * 45.0f,
                        fp.z
                    );
                    float sx = 0.0f, sy = 0.0f;
                    if (world_to_screen(wx, &sx, &sy, view_matrix, overlay->get_width(), overlay->get_height())) {
                        screen_points.push_back(ImVec2(sx, sy));
                    }
                }
            }

            if (screen_points.size() >= 3) {
                std::vector<ImVec2> hull = get_convex_hull(screen_points);
                if (hull.size() >= 3) {
                    draw_list->AddConvexPolyFilled(hull.data(), hull.size(), imcol32_from_color(cfg.inferno_fill_color));
                    draw_list->AddPolyline(hull.data(), hull.size(), imcol32_from_color(cfg.inferno_outline_color), ImDrawFlags_Closed, 2.0f);
                }
            }

            // Average fire positions for badge center
            Vec3 center(0.0f);
            for (int j = 0; j < inf_data.fire_count; ++j) {
                center += inf_data.fire_positions[j];
            }
            if (inf_data.fire_count > 0) {
                center /= static_cast<float>(inf_data.fire_count);
            }

            float csx = 0.0f, csy = 0.0f;
            if (world_to_screen(center, &csx, &csy, view_matrix, overlay->get_width(), overlay->get_height())) {
                draw_badge_with_timer(draw_list, csx, csy, GRENADE_MOLOTOV, remaining, inf_data.duration, imcol32_from_color(cfg.grenade_color_molotov), 1.0f, svg_cache);
            }
        }

        // 2. Render warning badges for in-flight active projectiles
        for (const auto& warn : tracked_warnings) {
            if (warn.fade_alpha <= 0.0f) continue;

            float sx = 0.0f, sy = 0.0f;
            if (world_to_screen(warn.position, &sx, &sy, view_matrix, overlay->get_width(), overlay->get_height())) {
                float remaining = 0.0f;
                float max_duration = 0.0f;

                if (warn.type == GRENADE_HE || warn.type == GRENADE_FLASH || warn.type == GRENADE_MOLOTOV) {
                    float duration = (warn.type == GRENADE_MOLOTOV) ? 2.02f : 1.5f;
                    remaining = std::max(0.0f, duration - static_cast<float>(current_real_time - warn.first_seen_time));
                    max_duration = duration;
                } else if (warn.type == GRENADE_SMOKE || warn.type == GRENADE_DECOY) {
                    if (warn.timer_trigger_real_time > 0.0) {
                        float elapsed = static_cast<float>(current_real_time - warn.timer_trigger_real_time);
                        remaining = std::max(0.0f, warn.timer_duration - elapsed);
                        max_duration = warn.timer_duration;
                    } else {
                        remaining = warn.timer_duration;
                        max_duration = warn.timer_duration;
                    }
                }

                ImU32 badge_color = imcol32_from_color(get_grenade_color(warn.type, cfg), warn.fade_alpha);
                draw_badge_with_timer(draw_list, sx, sy, warn.type, remaining, max_duration, badge_color, warn.fade_alpha, svg_cache);

                if (warn.affected_count > 0 && (warn.type == GRENADE_HE || warn.type == GRENADE_FLASH || warn.type == GRENADE_MOLOTOV)) {
                    draw_impact_warning(draw_list, sx, sy + 48.0f, warn.affected_count, warn.fade_alpha);
                }
            }
        }
    }

    // Prune GPU trajectory buffers that are no longer active or fading
    std::vector<uint32_t> active_handles;
    for (const auto& tracked : tracked_active_trajectories) {
        if (tracked.still_active) {
            active_handles.push_back(tracked.entity_handle);
        }
    }
    for (const auto& fading : fading_trajectories) {
        active_handles.push_back(fading.traj.entity_handle);
    }
    esp_renderer.prune_gpu_trajectories(active_handles);
}
