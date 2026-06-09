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
                     float glow_intensity = 1.0f);

    void end();

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

    bool compile_shader(unsigned int shader, const char* source);
    bool link_program();
};
