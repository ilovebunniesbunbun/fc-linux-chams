#pragma once
#include <vector>
#include "../vpk/vmdl/model.hpp"

namespace source2 {
    struct Mat3x4;
}

class GpuChamsRenderer {
public:
    GpuChamsRenderer() = default;
    ~GpuChamsRenderer();

    bool init();
    void cleanup();

    void begin(const float* view_proj, const float* cam_pos);
    
    // Render cache mesh with specific chams styles and bone updates
    void render_mesh(unsigned int vao, unsigned int ibo, size_t index_count,
                     const std::vector<source2::Mat3x4>& bones_palette,
                     const float* color, int style,
                     const float* glow_color = nullptr,
                     float glow_thickness = 0.0f,
                     float glow_intensity = 1.0f,
                     float glow_blur = 0.0f);

    void end();

    // Screen-space Glow Blur API
    void begin_glow_pass(int width, int height, const float* view_proj, const float* cam_pos);
    void render_glow_silhouette(unsigned int vao, unsigned int ibo, size_t index_count,
                                 const std::vector<source2::Mat3x4>& bones_palette,
                                 const float* color);
    void end_glow_pass(int width, int height, float thickness, float intensity, unsigned int target_fbo = 0);

    void begin_body_pass(const float* view_proj, const float* cam_pos);
    void end_body_pass();

private:
    unsigned int program_id = 0;
    unsigned int vertex_shader = 0;
    unsigned int fragment_shader = 0;

    int loc_view_proj = -1;
    int loc_bones = -1;
    int loc_color = -1;
    int loc_style = -1;
    int loc_cam_pos = -1;
    int loc_glow_color = -1;
    int loc_glow_thickness = -1;
    int loc_glow_intensity = -1;
    int loc_glow_blur = -1;

    // Glow/Blur FBO and shader resources
    unsigned int glow_fbo_silhouette = 0;
    unsigned int glow_tex_silhouette = 0;
    unsigned int glow_fbo_mask = 0;
    unsigned int glow_tex_mask = 0;
    unsigned int glow_fbo_a = 0;
    unsigned int glow_tex_a = 0;
    unsigned int glow_fbo_b = 0;
    unsigned int glow_tex_b = 0;
    int glow_last_w = 0;
    int glow_last_h = 0;

    unsigned int seed_program = 0;
    unsigned int seed_fs = 0;
    int loc_seed_texture = -1;
    int loc_seed_dir = -1;

    unsigned int blur_program = 0;
    unsigned int blur_vs = 0; // vertex shader shared for fullscreen passes
    unsigned int blur_fs = 0;
    int loc_blur_texture = -1;
    int loc_blur_dir = -1;

    unsigned int composite_program = 0;
    unsigned int composite_fs = 0;
    int loc_composite_glow_tex = -1;
    int loc_composite_mask_tex = -1;
    int loc_composite_dir = -1;
    int loc_composite_intensity = -1;

    unsigned int quad_vao = 0;
    unsigned int quad_vbo = 0;

    bool compile_shader(unsigned int shader, const char* source);
    bool link_program();
    
    void update_fbos(int width, int height);
    void cleanup_fbos();
    bool init_blur_shader();
    bool init_seed_shader(unsigned int vs);
    bool init_composite_shader(unsigned int vs);
};

