#include "esp_renderer.hpp"
#include "gl_loader.hpp"
#include <iostream>
#include <cmath>
#include <cstring>

static const char* esp_vertex_shader = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
out vec4 vColor;
uniform mat4 uProj;
void main() {
    gl_Position = uProj * vec4(aPos, 1.0);
    vColor = aColor;
}
)glsl";

static const char* esp_fragment_shader = R"glsl(
#version 330 core
in vec4 vColor;
out vec4 fragColor;
uniform vec4 uColorOverride;
uniform int uUseOverride;
void main() {
    if (uUseOverride != 0) {
        fragColor = uColorOverride;
    } else {
        fragColor = vColor;
    }
}
)glsl";

static Vec3 catmull_rom_3d(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return {
        0.5f * ((2.0f * p1.x) + (-p0.x + p2.x) * t + (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 + (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3),
        0.5f * ((2.0f * p1.y) + (-p0.y + p2.y) * t + (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 + (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3),
        0.5f * ((2.0f * p1.z) + (-p0.z + p2.z) * t + (2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 + (-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3)
    };
}

static float catmull_rom_1d(float p0, float p1, float p2, float p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

EspRenderer::~EspRenderer() {
    cleanup();
}

bool EspRenderer::compile_shader(unsigned int shader, const char* source) {
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cerr << "ESP_RENDERER: Shader compilation error: " << log << std::endl;
        return false;
    }
    return true;
}

bool EspRenderer::link_program() {
    program_id = glCreateProgram();
    glAttachShader(program_id, vertex_shader);
    glAttachShader(program_id, fragment_shader);
    glLinkProgram(program_id);
    int success;
    glGetProgramiv(program_id, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(program_id, 512, nullptr, log);
        std::cerr << "ESP_RENDERER: Shader linking error: " << log << std::endl;
        return false;
    }
    return true;
}

bool EspRenderer::init() {
    vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    if (!compile_shader(vertex_shader, esp_vertex_shader)) return false;

    fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    if (!compile_shader(fragment_shader, esp_fragment_shader)) return false;

    if (!link_program()) return false;

    loc_proj = glGetUniformLocation(program_id, "uProj");
    loc_color_override = glGetUniformLocation(program_id, "uColorOverride");
    loc_use_override = glGetUniformLocation(program_id, "uUseOverride");

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(EspVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(EspVertex), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}

void EspRenderer::cleanup() {
    clear();
    if (vao) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    if (vbo) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (vertex_shader) {
        glDeleteShader(vertex_shader);
        vertex_shader = 0;
    }
    if (fragment_shader) {
        glDeleteShader(fragment_shader);
        fragment_shader = 0;
    }
    if (program_id) {
        glDeleteProgram(program_id);
        program_id = 0;
    }
}

void EspRenderer::clear() {
    for (auto& p : line_batches) {
        p.second.clear();
    }
    line_batches.clear();
    triangle_vertices.clear();
}

void EspRenderer::set_projection(const float* proj_matrix) {
    std::memcpy(current_proj, proj_matrix, sizeof(float) * 16);
}

void EspRenderer::set_ortho(float left, float right, float bottom, float top) {
    float ortho[16] = {
        2.0f / (right - left), 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / (top - bottom), 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -(right + left) / (right - left), -(top + bottom) / (top - bottom), 0.0f, 1.0f
    };
    set_projection(ortho);
}

void EspRenderer::add_line_2d(float x1, float y1, float x2, float y2, const float* color, float thickness) {
    std::vector<EspVertex>* batch = nullptr;
    for (auto& p : line_batches) {
        if (p.first == thickness) { batch = &p.second; break; }
    }
    if (!batch) {
        line_batches.emplace_back(thickness, std::vector<EspVertex>{});
        batch = &line_batches.back().second;
    }
    batch->push_back({x1, y1, 0.0f, color[0], color[1], color[2], color[3]});
    batch->push_back({x2, y2, 0.0f, color[0], color[1], color[2], color[3]});
}

void EspRenderer::add_quad_2d(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, const float* color) {
    triangle_vertices.push_back({x1, y1, 0.0f, color[0], color[1], color[2], color[3]});
    triangle_vertices.push_back({x2, y2, 0.0f, color[0], color[1], color[2], color[3]});
    triangle_vertices.push_back({x3, y3, 0.0f, color[0], color[1], color[2], color[3]});

    triangle_vertices.push_back({x1, y1, 0.0f, color[0], color[1], color[2], color[3]});
    triangle_vertices.push_back({x3, y3, 0.0f, color[0], color[1], color[2], color[3]});
    triangle_vertices.push_back({x4, y4, 0.0f, color[0], color[1], color[2], color[3]});
}

void EspRenderer::add_rect_2d(float x, float y, float w, float h, const float* color) {
    add_quad_2d(x, y, x + w, y, x + w, y + h, x, y + h, color);
}

void EspRenderer::add_outlined_rect_2d(float x, float y, float w, float h, const float* color, float thickness, bool draw_outline) {
    float ix = std::round(x) + 0.5f;
    float iy = std::round(y) + 0.5f;
    float iw = std::round(w);
    float ih = std::round(h);
    float t = std::round(thickness);
    if (t < 1.0f) t = 1.0f;

    if (draw_outline) {
        float black[4] = {0.0f, 0.0f, 0.0f, color[3]};
        // Black outer outline: 1 pixel outside
        add_line_2d(ix - t, iy - t, ix + iw + t, iy - t, black, 1.0f);
        add_line_2d(ix + iw + t, iy - t, ix + iw + t, iy + ih + t, black, 1.0f);
        add_line_2d(ix + iw + t, iy + ih + t, ix - t, iy + ih + t, black, 1.0f);
        add_line_2d(ix - t, iy + ih + t, ix - t, iy - t, black, 1.0f);

        // Black inner outline: 1 pixel inside
        if (iw > 2.0f * t && ih > 2.0f * t) {
            add_line_2d(ix + t, iy + t, ix + iw - t, iy + t, black, 1.0f);
            add_line_2d(ix + iw - t, iy + t, ix + iw - t, iy + ih - t, black, 1.0f);
            add_line_2d(ix + iw - t, iy + ih - t, ix + t, iy + ih - t, black, 1.0f);
            add_line_2d(ix + t, iy + ih - t, ix + t, iy + t, black, 1.0f);
        }
    }

    // Main box
    add_line_2d(ix, iy, ix + iw, iy, color, t);
    add_line_2d(ix + iw, iy, ix + iw, iy + ih, color, t);
    add_line_2d(ix + iw, iy + ih, ix, iy + ih, color, t);
    add_line_2d(ix, iy + ih, ix, iy, color, t);
}

void EspRenderer::add_health_bar_2d(float x, float y, float w, float h, float health, const OverlayConfig& cfg) {
    (void)w;
    if (health < 0.0f) health = 0.0f;
    if (health > 100.0f) health = 100.0f;

    float ix = std::round(x);
    float iy = std::round(y);
    float ih = std::round(h);
    float bar_w = std::round(cfg.esp_health_bar_thickness);
    if (bar_w < 1.0f) bar_w = 1.0f;

    float bar_x = ix - bar_w - 4.0f;
    float bar_y = iy;
    float bar_h = ih;

    float hp_pct = health / 100.0f;
    float fill_h = std::round(bar_h * hp_pct);
    float fill_y = bar_y + (bar_h - fill_h);

    if (cfg.esp_health_bar_outline) {
        float black[4] = {0.0f, 0.0f, 0.0f, cfg.esp_health_bar_color[3]};
        add_rect_2d(bar_x - 1.0f, bar_y - 1.0f, bar_w + 2.0f, bar_h + 2.0f, black);
    }

    float r = cfg.esp_health_bar_color[0];
    float g = cfg.esp_health_bar_color[1];
    float b = cfg.esp_health_bar_color[2];
    float a = cfg.esp_health_bar_color[3];
    if (cfg.esp_health_bar_gradient) {
        r = cfg.esp_health_bar_gradient_start[0] * hp_pct + cfg.esp_health_bar_gradient_end[0] * (1.0f - hp_pct);
        g = cfg.esp_health_bar_gradient_start[1] * hp_pct + cfg.esp_health_bar_gradient_end[1] * (1.0f - hp_pct);
        b = cfg.esp_health_bar_gradient_start[2] * hp_pct + cfg.esp_health_bar_gradient_end[2] * (1.0f - hp_pct);
        a = cfg.esp_health_bar_gradient_start[3] * hp_pct + cfg.esp_health_bar_gradient_end[3] * (1.0f - hp_pct);
    }

    float color[4] = {r, g, b, a};
    add_rect_2d(bar_x, fill_y, bar_w, fill_h, color);
}

void EspRenderer::add_line_3d(const Vec3& p1, const Vec3& p2, const float* color, float thickness) {
    std::vector<EspVertex>* batch = nullptr;
    for (auto& p : line_batches) {
        if (p.first == thickness) { batch = &p.second; break; }
    }
    if (!batch) {
        line_batches.emplace_back(thickness, std::vector<EspVertex>{});
        batch = &line_batches.back().second;
    }
    batch->push_back({p1.x, p1.y, p1.z, color[0], color[1], color[2], color[3]});
    batch->push_back({p2.x, p2.y, p2.z, color[0], color[1], color[2], color[3]});
}

void EspRenderer::add_skeleton_chain_3d(const std::vector<Vec3>& sanitized_positions, const std::vector<int>& chain, const std::vector<float>& vis, int player_idx, const OverlayConfig& cfg, bool for_glow, float pulse_factor, const float* override_glow_color) {
    Vec3 points[8];
    float bone_vis[8];
    int point_count = 0;

    for (int b : chain) {
        if (b >= static_cast<int>(sanitized_positions.size())) continue;
        if (point_count >= 8) break;
        points[point_count] = sanitized_positions[b];

        int vis_idx = player_idx * 128 + b;
        if (vis_idx < static_cast<int>(vis.size())) {
            bone_vis[point_count] = vis[vis_idx];
        } else {
            bone_vis[point_count] = 1.0f;
        }
        point_count++;
    }

    if (point_count < 2) return;

    float thickness = cfg.esp_skeleton_thickness;

    if (cfg.esp_rounded_skeleton) {
        Vec3 ctrl_pts[10]; // max 8 + 2 padding
        float ctrl_vis[10];
        ctrl_pts[0] = points[0];
        ctrl_vis[0] = bone_vis[0];
        for (int i = 0; i < point_count; ++i) {
            ctrl_pts[i + 1] = points[i];
            ctrl_vis[i + 1] = bone_vis[i];
        }
        ctrl_pts[point_count + 1] = points[point_count - 1];
        ctrl_vis[point_count + 1] = bone_vis[point_count - 1];
        int ctrl_count = point_count + 2;

        Vec3 last_pt;
        float last_v = 0.0f;
        bool first = true;

        for (int i = 0; i + 3 < ctrl_count; ++i) {
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
                    add_line_3d(last_pt, pt, color, thickness);
                } else {
                    float mid_v = (last_v + v) * 0.5f;
                    const float* color = (mid_v > 0.5f) ? cfg.esp_skeleton_color_vis : cfg.esp_skeleton_color_invis;
                    add_line_3d(last_pt, pt, color, thickness);
                }

                last_pt = pt;
                last_v = v;
            }
        }
    } else {
        for (int i = 0; i < point_count - 1; ++i) {
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
                add_line_3d(pt1, pt2, color, thickness);
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

                    const float* color = (mid_v > 0.5f) ? cfg.esp_skeleton_color_vis : cfg.esp_skeleton_color_invis;
                    add_line_3d(prev_step, curr_step, color, thickness);

                    prev_step = curr_step;
                    prev_v = curr_v;
                }
            }
        }
    }
}

void EspRenderer::add_skeleton_3d(const std::vector<Vec3>& sanitized_positions, int player_idx, const std::vector<float>& vis, const OverlayConfig& cfg, bool for_glow, float pulse_factor, const float* override_glow_color) {
    static const std::vector<std::vector<int>> bone_chains = {
        {7, 6, 23, 1},
        {23, 10, 11},
        {23, 14, 15},
        {1, 18, 19},
        {1, 21, 22}
    };

    for (const auto& chain : bone_chains) {
        add_skeleton_chain_3d(sanitized_positions, chain, vis, player_idx, cfg, for_glow, pulse_factor, override_glow_color);
    }
}

void EspRenderer::flush_lines() {
    if (line_batches.empty()) return;

    glUseProgram(program_id);
    glUniformMatrix4fv(loc_proj, 1, GL_FALSE, current_proj);
    glUniform1i(loc_use_override, 0);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    for (const auto& pair : line_batches) {
        float thickness = pair.first;
        const auto& vertices = pair.second;
        if (vertices.empty()) continue;

        glLineWidth(thickness);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(EspVertex), vertices.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_LINES, 0, (GLsizei)vertices.size());
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void EspRenderer::flush_lines_override(const float* override_color) {
    if (line_batches.empty()) return;

    glUseProgram(program_id);
    glUniformMatrix4fv(loc_proj, 1, GL_FALSE, current_proj);
    glUniform1i(loc_use_override, 1);
    glUniform4fv(loc_color_override, 1, override_color);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    for (const auto& pair : line_batches) {
        float thickness = pair.first;
        const auto& vertices = pair.second;
        if (vertices.empty()) continue;

        glLineWidth(thickness);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(EspVertex), vertices.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_LINES, 0, (GLsizei)vertices.size());
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}

void EspRenderer::flush_triangles() {
    if (triangle_vertices.empty()) return;

    glUseProgram(program_id);
    glUniformMatrix4fv(loc_proj, 1, GL_FALSE, current_proj);
    glUniform1i(loc_use_override, 0);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(GL_ARRAY_BUFFER, triangle_vertices.size() * sizeof(EspVertex), triangle_vertices.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)triangle_vertices.size());

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
}
