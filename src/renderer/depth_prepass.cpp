#include "depth_prepass.hpp"
#include "gl_loader.hpp"
#include <iostream>

static const char* depth_vertex_shader = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)glsl";

static const char* depth_fragment_shader = R"glsl(
#version 330 core
void main() {
    // Empty fragment shader: only depth writes are performed
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
    if (vertex_shader) {
        glDeleteShader(vertex_shader);
        vertex_shader = 0;
    }
    if (fragment_shader) {
        glDeleteShader(fragment_shader);
        fragment_shader = 0;
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
        std::cerr << "DEPTH_PREPASS: Shader compilation error: " << info_log << std::endl;
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
        std::cerr << "DEPTH_PREPASS: Shader linking error: " << info_log << std::endl;
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
    
    // Upload raw Triangle structures directly
    glBufferData(GL_ARRAY_BUFFER, triangles.size() * sizeof(MapParser::Triangle), triangles.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MapParser::Vec3), (void*)0);

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
