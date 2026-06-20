#include "depth_prepass.hpp"
#include "gl_loader.hpp"
#include "logger.hpp"

static const char* depth_vertex_shader = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aTypeID;
uniform mat4 uViewProj;
out float vTypeID;
void main() {
    vTypeID = aTypeID;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)glsl";

static const char* depth_fragment_shader = R"glsl(
#version 330 core
void main() {
    // Empty fragment shader: only depth writes are performed
}
)glsl";

static const char* wireframe_fragment_shader_src = R"glsl(
#version 330 core
out vec4 FragColor;
uniform vec4 uColor;
in float vTypeID;
void main() {
    vec4 finalColor = uColor;
    if (vTypeID > 0.5 && vTypeID < 1.5) {
        finalColor = vec4(1.0, 0.0, 0.0, uColor.a); // Breakables (Red)
    } else if (vTypeID > 1.5 && vTypeID < 2.5) {
        finalColor = vec4(0.0, 1.0, 0.0, uColor.a); // Physics props (Green)
    } else if (vTypeID > 2.5 && vTypeID < 3.5) {
        finalColor = vec4(0.0, 0.0, 1.0, uColor.a); // Func Brush (Blue)
    } else if (vTypeID > 3.5 && vTypeID < 4.5) {
        finalColor = vec4(1.0, 0.0, 1.0, uColor.a); // Func Clip VPhysics (Magenta)
    } else if (vTypeID > 4.5 && vTypeID < 5.5) {
        finalColor = vec4(0.0, 1.0, 1.0, uColor.a); // Func Physbox (Cyan)
    }
    FragColor = finalColor;
}
)glsl";

DepthPrepassRenderer::~DepthPrepassRenderer() {
    cleanup();
}

void DepthPrepassRenderer::cleanup() {
    clear_geometry();
    if (program_id) {
        glDeleteProgram(program_id);
        program_id = 0;
    }
    if (wireframe_program_id) {
        glDeleteProgram(wireframe_program_id);
        wireframe_program_id = 0;
    }
    if (vertex_shader) {
        glDeleteShader(vertex_shader);
        vertex_shader = 0;
    }
    if (fragment_shader) {
        glDeleteShader(fragment_shader);
        fragment_shader = 0;
    }
    if (wireframe_fragment_shader) {
        glDeleteShader(wireframe_fragment_shader);
        wireframe_fragment_shader = 0;
    }
}

void DepthPrepassRenderer::clear_geometry() {
    if (vbo) {
        glDeleteBuffers(1, &vbo);
        vbo = 0;
    }
    if (vao) {
        glDeleteVertexArrays(1, &vao);
        vao = 0;
    }
    vertex_count = 0;
}

bool DepthPrepassRenderer::compile_shader(unsigned int shader, const char* source) {
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(shader, 512, nullptr, info_log);
        FC2_LOG_ERROR("DEPTH_PREPASS: Shader compilation error: {}", info_log);
        return false;
    }
    return true;
}

bool DepthPrepassRenderer::link_program() {
    program_id = glCreateProgram();
    glAttachShader(program_id, vertex_shader);
    glAttachShader(program_id, fragment_shader);
    glLinkProgram(program_id);

    int success;
    glGetProgramiv(program_id, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(program_id, 512, nullptr, info_log);
        FC2_LOG_ERROR("DEPTH_PREPASS: Shader linking error: {}", info_log);
        return false;
    }
    return true;
}

bool DepthPrepassRenderer::link_wireframe_program() {
    wireframe_program_id = glCreateProgram();
    glAttachShader(wireframe_program_id, vertex_shader);
    glAttachShader(wireframe_program_id, wireframe_fragment_shader);
    glLinkProgram(wireframe_program_id);

    int success;
    glGetProgramiv(wireframe_program_id, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(wireframe_program_id, 512, nullptr, info_log);
        FC2_LOG_ERROR("MAP_VISUALIZER: Shader linking error: {}", info_log);
        return false;
    }
    return true;
}

bool DepthPrepassRenderer::init() {
    vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    if (!compile_shader(vertex_shader, depth_vertex_shader)) return false;

    fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    if (!compile_shader(fragment_shader, depth_fragment_shader)) return false;

    if (!link_program()) return false;

    loc_view_proj = glGetUniformLocation(program_id, "uViewProj");

    // Initialize map visualizer shaders
    wireframe_fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    if (!compile_shader(wireframe_fragment_shader, wireframe_fragment_shader_src)) return false;

    if (!link_wireframe_program()) return false;

    loc_wf_view_proj = glGetUniformLocation(wireframe_program_id, "uViewProj");
    loc_wf_color = glGetUniformLocation(wireframe_program_id, "uColor");

    return true;
}

void DepthPrepassRenderer::upload_geometry(const std::vector<MapParser::Triangle>& triangles) {
    clear_geometry();
    if (triangles.empty()) return;

    vertex_count = triangles.size() * 3;

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    
    // Upload raw Vertex structures directly (a Triangle is 3 Vertices)
    glBufferData(GL_ARRAY_BUFFER, triangles.size() * sizeof(MapParser::Triangle), triangles.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MapParser::Vertex), (void*)offsetof(MapParser::Vertex, pos));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(MapParser::Vertex), (void*)offsetof(MapParser::Vertex, type_id));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void DepthPrepassRenderer::render(const float* view_proj) {
    if (!has_geometry()) return;

    glUseProgram(program_id);
    glUniformMatrix4fv(loc_view_proj, 1, GL_FALSE, view_proj);

    // Disable color rendering: write only to depth buffer
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    // Render map geometry
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertex_count);
    
    // Restore state
    glBindVertexArray(0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(0);
}

void DepthPrepassRenderer::render_wireframe(const float* view_proj, const float* color, bool depth_tested) {
    if (!has_geometry()) return;

    glUseProgram(wireframe_program_id);
    glUniformMatrix4fv(loc_wf_view_proj, 1, GL_FALSE, view_proj);
    glUniform4fv(loc_wf_color, 1, color);

    // Render as line wireframe
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    // Enable blending for transparency support
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (depth_tested) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(GL_FALSE);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertex_count);

    // Restore state
    glBindVertexArray(0);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    glUseProgram(0);
}
