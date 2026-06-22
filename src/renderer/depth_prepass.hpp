#pragma once
#include <vector>
#include "../vpk/vmdl/maps/map_parser.hpp"
#include "../overlay/bvh_parser.hpp"

class DepthPrepassRenderer {
public:
    DepthPrepassRenderer() = default;
    ~DepthPrepassRenderer();

    bool init();
    void cleanup();

    void upload_geometry(const std::vector<MapParser::Triangle>& triangles);
    void upload_dynamic_doors(const std::vector<LocalMapBVH::Triangle>& triangles);
    void render(const float* view_proj);
    void render_wireframe(const float* view_proj, const float* color, bool depth_tested = true);

    bool has_geometry() const { return vertex_count > 0; }
    void clear_geometry();

private:
    unsigned int program_id = 0;
    unsigned int vertex_shader = 0;
    unsigned int fragment_shader = 0;

    unsigned int wireframe_program_id = 0;
    unsigned int wireframe_fragment_shader = 0;

    unsigned int vao = 0;
    unsigned int vbo = 0;
    size_t vertex_count = 0;

    unsigned int dynamic_vao = 0;
    unsigned int dynamic_vbo = 0;
    size_t dynamic_vertex_count = 0;

    int loc_view_proj = -1;
    int loc_wf_view_proj = -1;
    int loc_wf_color = -1;

    bool compile_shader(unsigned int shader, const char* source);
    bool link_program();
    bool link_wireframe_program();
};
