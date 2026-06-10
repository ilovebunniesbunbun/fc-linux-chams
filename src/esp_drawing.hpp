#pragma once
#include "renderer/gl_loader.hpp"
#include <GL/gl.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include "shm_reader.hpp"
#include "config.hpp"

inline Vec3 operator-(const Vec3& a, const Vec3& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline Vec3 operator*(const Vec3& a, float b) {
    return { a.x * b, a.y * b, a.z * b };
}

inline bool world_to_screen(const Vec3& world, float* screen_x, float* screen_y, const float* matrix, int width, int height) {
    float w = matrix[12] * world.x + matrix[13] * world.y + matrix[14] * world.z + matrix[15];
    if (w < 0.001f) return false;
    float x = matrix[0] * world.x + matrix[1] * world.y + matrix[2] * world.z + matrix[3];
    float y = matrix[4] * world.x + matrix[5] * world.y + matrix[6] * world.z + matrix[7];

    float inv_w = 1.0f / w;
    x *= inv_w;
    y *= inv_w;

    *screen_x = (width * 0.5f) + (x * width * 0.5f);
    *screen_y = (height * 0.5f) - (y * height * 0.5f);
    return true;
}

inline Vec3 catmull_rom_3d(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return {
        0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
        0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3),
        0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t + (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 + (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3)
    };
}

inline float catmull_rom_1d(float p0, float p1, float p2, float p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

inline void draw_outlined_rect(float x, float y, float w, float h, const float* color, float thickness, bool draw_outline) {
    // Snap to integer pixels and add 0.5f offset for perfect OpenGL pixel alignment
    float ix = std::round(x) + 0.5f;
    float iy = std::round(y) + 0.5f;
    float iw = std::round(w);
    float ih = std::round(h);
    float t = std::round(thickness);
    if (t < 1.0f) t = 1.0f;

    if (draw_outline) {
        // Black outer outline: 1 pixel outside the main box
        glColor4f(0.0f, 0.0f, 0.0f, color[3]);
        glLineWidth(1.0f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(ix - t, iy - t);
        glVertex2f(ix + iw + t, iy - t);
        glVertex2f(ix + iw + t, iy + ih + t);
        glVertex2f(ix - t, iy + ih + t);
        glEnd();

        // Black inner outline: 1 pixel inside the main box
        if (iw > 2.0f * t && ih > 2.0f * t) {
            glBegin(GL_LINE_LOOP);
            glVertex2f(ix + t, iy + t);
            glVertex2f(ix + iw - t, iy + t);
            glVertex2f(ix + iw - t, iy + ih - t);
            glVertex2f(ix + t, iy + ih - t);
            glEnd();
        }
    }

    // Main box
    glColor4fv(color);
    glLineWidth(t);
    glBegin(GL_LINE_LOOP);
    glVertex2f(ix, iy);
    glVertex2f(ix + iw, iy);
    glVertex2f(ix + iw, iy + ih);
    glVertex2f(ix, iy + ih);
    glEnd();
}

inline void draw_health_bar(float x, float y, float w, float h, float health, const OverlayConfig& cfg) {
    if (health < 0.0f) health = 0.0f;
    if (health > 100.0f) health = 100.0f;

    // Snap to integer pixels for perfect alignment with the box
    float ix = std::round(x);
    float iy = std::round(y);
    float iw = std::round(w);
    float ih = std::round(h);
    float bar_w = std::round(cfg.esp_health_bar_thickness);
    if (bar_w < 1.0f) bar_w = 1.0f;

    float bar_x = ix - bar_w - 4.0f; // Positioned 4px to the left of the box
    float bar_y = iy;
    float bar_h = ih;

    float hp_pct = health / 100.0f;
    float fill_h = std::round(bar_h * hp_pct);
    float fill_y = bar_y + (bar_h - fill_h);

    if (cfg.esp_health_bar_outline) {
        // Black background/outline
        glColor4f(0.0f, 0.0f, 0.0f, cfg.esp_health_bar_color[3]);
        glBegin(GL_QUADS);
        glVertex2f(bar_x - 1.0f, bar_y - 1.0f);
        glVertex2f(bar_x + bar_w + 1.0f, bar_y - 1.0f);
        glVertex2f(bar_x + bar_w + 1.0f, bar_y + bar_h + 1.0f);
        glVertex2f(bar_x - 1.0f, bar_y + bar_h + 1.0f);
        glEnd();
    }

    // Health color
    float r = cfg.esp_health_bar_color[0];
    float g = cfg.esp_health_bar_color[1];
    float b = cfg.esp_health_bar_color[2];
    float a = cfg.esp_health_bar_color[3];
    if (cfg.esp_health_bar_gradient) {
        // Interpolate between custom start and end colors
        r = cfg.esp_health_bar_gradient_start[0] * hp_pct + cfg.esp_health_bar_gradient_end[0] * (1.0f - hp_pct);
        g = cfg.esp_health_bar_gradient_start[1] * hp_pct + cfg.esp_health_bar_gradient_end[1] * (1.0f - hp_pct);
        b = cfg.esp_health_bar_gradient_start[2] * hp_pct + cfg.esp_health_bar_gradient_end[2] * (1.0f - hp_pct);
        a = cfg.esp_health_bar_gradient_start[3] * hp_pct + cfg.esp_health_bar_gradient_end[3] * (1.0f - hp_pct);
    }

    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(bar_x, fill_y);
    glVertex2f(bar_x + bar_w, fill_y);
    glVertex2f(bar_x + bar_w, bar_y + bar_h);
    glVertex2f(bar_x, bar_y + bar_h);
    glEnd();
}

inline void draw_skeleton_chain(const std::vector<Vec3>& sanitized_positions, const std::vector<int>& chain, const std::vector<float>& vis, int player_idx, const OverlayConfig& cfg, bool has_prepass, bool for_glow, float pulse_factor, const float* override_glow_color = nullptr) {
    std::vector<Vec3> points;
    std::vector<float> bone_vis;

    for (int b : chain) {
        if (b >= static_cast<int>(sanitized_positions.size())) continue;
        points.push_back(sanitized_positions[b]);

        int vis_idx = player_idx * 128 + b;
        if (vis_idx < static_cast<int>(vis.size())) {
            bone_vis.push_back(vis[vis_idx]);
        } else {
            bone_vis.push_back(1.0f); // Default to visible
        }
    }

    if (points.size() < 2) return;

    if (cfg.esp_rounded_skeleton) {
        // Prepend front and append back for control points
        std::vector<Vec3> ctrl_pts = points;
        ctrl_pts.insert(ctrl_pts.begin(), points.front());
        ctrl_pts.push_back(points.back());

        std::vector<float> ctrl_vis = bone_vis;
        ctrl_vis.insert(ctrl_vis.begin(), bone_vis.front());
        ctrl_vis.push_back(bone_vis.back());

        Vec3 last_pt;
        float last_v = 0.0f;
        bool first = true;

        for (size_t i = 0; i + 3 < ctrl_pts.size(); ++i) {
            const auto& p0 = ctrl_pts[i];
            const auto& p1 = ctrl_pts[i + 1];
            const auto& p2 = ctrl_pts[i + 2];
            const auto& p3 = ctrl_pts[i + 3];

            float v0 = ctrl_vis[i];
            float v1 = ctrl_vis[i + 1];
            float v2 = ctrl_vis[i + 2];
            float v3 = ctrl_vis[i + 3];

            for (int step = 0; step <= 20; ++step) {
                float t = static_cast<float>(step) / 20.0f;
                Vec3 pt = catmull_rom_3d(p0, p1, p2, p3, t);
                float v = catmull_rom_1d(v0, v1, v2, v3, t);

                if (first) {
                    last_pt = pt;
                    last_v = v;
                    first = false;
                    continue;
                }

                if (for_glow) {
                    float color[4];
                    if (override_glow_color) {
                        std::memcpy(color, override_glow_color, sizeof(float) * 4);
                    } else {
                        std::memcpy(color, cfg.esp_skeleton_glow_color, sizeof(float) * 4);
                    }
                    color[3] *= pulse_factor;
                    glColor4fv(color);
                    glBegin(GL_LINES);
                    glVertex3f(last_pt.x, last_pt.y, last_pt.z);
                    glVertex3f(pt.x, pt.y, pt.z);
                    glEnd();
                } else if (has_prepass) {
                    glBegin(GL_LINES);
                    glVertex3f(last_pt.x, last_pt.y, last_pt.z);
                    glVertex3f(pt.x, pt.y, pt.z);
                    glEnd();
                } else {
                    float mid_v = (last_v + v) * 0.5f;
                    if (mid_v > 0.5f) {
                        glColor4fv(cfg.esp_skeleton_color_vis);
                    } else {
                        glColor4fv(cfg.esp_skeleton_color_invis);
                    }
                    glBegin(GL_LINES);
                    glVertex3f(last_pt.x, last_pt.y, last_pt.z);
                    glVertex3f(pt.x, pt.y, pt.z);
                    glEnd();
                }

                last_pt = pt;
                last_v = v;
            }
        }
    } else {
        for (size_t i = 0; i < points.size() - 1; ++i) {
            const auto& pt1 = points[i];
            const auto& pt2 = points[i + 1];
            float v1 = bone_vis[i];
            float v2 = bone_vis[i + 1];

            if (for_glow) {
                float color[4];
                if (override_glow_color) {
                    std::memcpy(color, override_glow_color, sizeof(float) * 4);
                } else {
                    std::memcpy(color, cfg.esp_skeleton_glow_color, sizeof(float) * 4);
                }
                color[3] *= pulse_factor;
                glColor4fv(color);
                glBegin(GL_LINES);
                glVertex3f(pt1.x, pt1.y, pt1.z);
                glVertex3f(pt2.x, pt2.y, pt2.z);
                glEnd();
            } else if (has_prepass) {
                glBegin(GL_LINES);
                glVertex3f(pt1.x, pt1.y, pt1.z);
                glVertex3f(pt2.x, pt2.y, pt2.z);
                glEnd();
            } else {
                const int steps = 5;
                Vec3 prev_step = pt1;
                float prev_v = v1;

                for (int s = 1; s <= steps; ++s) {
                    float t = static_cast<float>(s) / steps;
                    Vec3 curr_step = {
                        pt1.x + t * (pt2.x - pt1.x),
                        pt1.y + t * (pt2.y - pt1.y),
                        pt1.z + t * (pt2.z - pt1.z)
                    };
                    float curr_v = v1 + t * (v2 - v1);
                    float mid_v = (prev_v + curr_v) * 0.5f;

                    if (mid_v > 0.5f) {
                        glColor4fv(cfg.esp_skeleton_color_vis);
                    } else {
                        glColor4fv(cfg.esp_skeleton_color_invis);
                    }

                    glBegin(GL_LINES);
                    glVertex3f(prev_step.x, prev_step.y, prev_step.z);
                    glVertex3f(curr_step.x, curr_step.y, curr_step.z);
                    glEnd();

                    prev_step = curr_step;
                    prev_v = curr_v;
                }
            }
        }
    }
}

inline void draw_skeleton(const std::vector<Vec3>& sanitized_positions, int player_idx, const std::vector<float>& vis, const float* gl_vp, const OverlayConfig& cfg, bool has_prepass, bool for_glow, float pulse_factor, const float* override_glow_color = nullptr) {
    if (for_glow) {
        float prev_lw;
        glGetFloatv(GL_LINE_WIDTH, &prev_lw);
        glLineWidth(cfg.esp_skeleton_thickness);

        const std::vector<std::vector<int>> bone_chains = {
            {7, 6, 23, 1},
            {23, 10, 11},
            {23, 14, 15},
            {1, 18, 19},
            {1, 21, 22}
        };

        for (const auto& chain : bone_chains) {
            draw_skeleton_chain(sanitized_positions, chain, vis, player_idx, cfg, has_prepass, true, pulse_factor, override_glow_color);
        }

        glLineWidth(prev_lw);
        return;
    }

    glDisable(GL_TEXTURE_2D);
    glUseProgram(0);

    float prev_lw;
    glGetFloatv(GL_LINE_WIDTH, &prev_lw);
    glLineWidth(cfg.esp_skeleton_thickness);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadMatrixf(gl_vp);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    const std::vector<std::vector<int>> bone_chains = {
        {7, 6, 23, 1},
        {23, 10, 11},
        {23, 14, 15},
        {1, 18, 19},
        {1, 21, 22}
    };

    if (has_prepass) {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        // Pass 1: Hidden
        glDepthFunc(GL_GREATER);
        glColor4fv(cfg.esp_skeleton_color_invis);
        for (const auto& chain : bone_chains) {
            draw_skeleton_chain(sanitized_positions, chain, vis, player_idx, cfg, true, false, 1.0f);
        }

        // Pass 2: Visible
        glDepthFunc(GL_LEQUAL);
        glColor4fv(cfg.esp_skeleton_color_vis);
        for (const auto& chain : bone_chains) {
            draw_skeleton_chain(sanitized_positions, chain, vis, player_idx, cfg, true, false, 1.0f);
        }

        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LEQUAL);
    } else {
        glDisable(GL_DEPTH_TEST);
        for (const auto& chain : bone_chains) {
            draw_skeleton_chain(sanitized_positions, chain, vis, player_idx, cfg, false, false, 1.0f);
        }
    }

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    glLineWidth(prev_lw);
}

inline bool get_player_bounds(const std::vector<Vec3>& sanitized_positions, const Vec3& origin, const Vec3& cam_pos, const float* matrix, int width, int height, float& box_x, float& box_y, float& box_w, float& box_h, const OverlayConfig& cfg) {
    int active_mode = cfg.esp_box_mode;

    if (active_mode == 2) {
        Vec3 delta = origin - cam_pos;
        float dist = std::sqrt(delta.x*delta.x + delta.y*delta.y + delta.z*delta.z);
        if (dist < 10.0f) dist = 10.0f;

        float bw = cfg.esp_box_static_w / dist;
        float bh = cfg.esp_box_static_h / dist;

        float sx, sy;
        if (world_to_screen(origin, &sx, &sy, matrix, width, height)) {
            box_x = sx - bw * 0.5f;
            box_y = sy - bh;
            box_w = bw;
            box_h = bh;
            return true;
        }
        return false;
    }

    if (active_mode == 1) {
        // Dynamic (Feet + Head) - Jitter Free!
        if (7 >= static_cast<int>(sanitized_positions.size())) return false;
        const Vec3& head_pos = sanitized_positions[7];

        // Add 6.0f vertically to offset the box to the top of head/helmet
        Vec3 top_pos = head_pos;
        top_pos.z += 6.0f;

        float feet_x, feet_y;
        float head_x, head_y;
        if (!world_to_screen(origin, &feet_x, &feet_y, matrix, width, height)) return false;
        if (!world_to_screen(top_pos, &head_x, &head_y, matrix, width, height)) return false;

        float h = feet_y - head_y;
        if (h < 2.0f) return false;

        float w = h * 0.54f; // Proportional box aspect ratio
        float center_x = (feet_x + head_x) * 0.5f;

        box_x = center_x - w * 0.5f;
        box_y = head_y;
        box_w = w;
        box_h = h;
        return true;
    }

    // active_mode == 0: Dynamic (Bones)
    float min_x = 99999.0f;
    float max_x = -99999.0f;
    float min_y = 99999.0f;
    float max_y = -99999.0f;
    bool any_visible = false;

    const int bones_to_check[] = {7, 6, 23, 1, 10, 11, 14, 15, 18, 19, 21, 22};
    for (int b : bones_to_check) {
        if (b >= static_cast<int>(sanitized_positions.size())) continue;
        float sx, sy;
        if (world_to_screen(sanitized_positions[b], &sx, &sy, matrix, width, height)) {
            if (sx < min_x) min_x = sx;
            if (sx > max_x) max_x = sx;
            if (sy < min_y) min_y = sy;
            if (sy > max_y) max_y = sy;
            any_visible = true;
        }
    }

    if (!any_visible) return false;

    float pad_w = (max_x - min_x) * 0.15f;
    float pad_h = (max_y - min_y) * 0.10f;

    if (pad_w < 5.0f) pad_w = 5.0f;
    if (pad_h < 5.0f) pad_h = 5.0f;

    box_x = min_x - pad_w;
    box_y = min_y - pad_h;
    box_w = (max_x - min_x) + 2.0f * pad_w;
    box_h = (max_y - min_y) + 2.0f * pad_h;
    return true;
}
