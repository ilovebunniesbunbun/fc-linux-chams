#pragma once
#include <vector>
#include "../vpk/vmdl/maps/map_parser.hpp"

class DepthPrepassRenderer {
public:
    DepthPrepassRenderer() = default;
    ~DepthPrepassRenderer();

    bool init();
    void cleanup();

    void upload_geometry(const std::vector<MapParser::Triangle>& triangles);
    void render(const float* view_proj);

    bool has_geometry() const { return vertex_count > 0; }
    void clear_geometry();

private:
    unsigned int program_id = 0;
    unsigned int vertex_shader = 0;
    unsigned int fragment_shader = 0;

    unsigned int vao = 0;
    unsigned int vbo = 0;
    size_t vertex_count = 0;

    int loc_view_proj = -1;

    bool compile_shader(unsigned int shader, const char* source);
    bool link_program();
};
