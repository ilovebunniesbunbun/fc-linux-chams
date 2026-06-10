#pragma once
#include <vector>
#include <map>
#include "shm_reader.hpp"
#include "config.hpp"

struct EspVertex {
    float x, y, z;
    float r, g, b, a;
};

class EspRenderer {
public:
    EspRenderer() = default;
    ~EspRenderer();

    bool init();
    void cleanup();

    // Clear accumulated vertex lists
    void clear();

    // Matrix and projection setup
    void set_projection(const float* proj_matrix);
    void set_ortho(float left, float right, float bottom, float top);

    // 2D Shape builders
    void add_line_2d(float x1, float y1, float x2, float y2, const float* color, float thickness = 1.0f);
    void add_rect_2d(float x, float y, float w, float h, const float* color);
    void add_outlined_rect_2d(float x, float y, float w, float h, const float* color, float thickness, bool draw_outline);
    void add_health_bar_2d(float x, float y, float w, float h, float health, const OverlayConfig& cfg);

    // 3D Skeleton builders
    void add_line_3d(const Vec3& p1, const Vec3& p2, const float* color, float thickness = 1.0f);
    void add_skeleton_chain_3d(const std::vector<Vec3>& sanitized_positions, const std::vector<int>& chain, const std::vector<float>& vis, int player_idx, const OverlayConfig& cfg, bool for_glow, float pulse_factor, const float* override_glow_color);
    void add_skeleton_3d(const std::vector<Vec3>& sanitized_positions, int player_idx, const std::vector<float>& vis, const OverlayConfig& cfg, bool for_glow, float pulse_factor, const float* override_glow_color);

    // Render flushing
    void flush_lines();
    void flush_lines_override(const float* override_color);
    void flush_triangles();

private:
    unsigned int program_id = 0;
    unsigned int vertex_shader = 0;
    unsigned int fragment_shader = 0;
    unsigned int vao = 0;
    unsigned int vbo = 0;
    
    int loc_proj = -1;
    int loc_color_override = -1;
    int loc_use_override = -1;

    // We store line vertices grouped by line thickness
    std::map<float, std::vector<EspVertex>> line_batches;
    std::vector<EspVertex> triangle_vertices;

    float current_proj[16];

    bool compile_shader(unsigned int shader, const char* source);
    bool link_program();
    void add_quad_2d(float x1, float y1, float x2, float y2, float x3, float y3, float x4, float y4, const float* color);
};
