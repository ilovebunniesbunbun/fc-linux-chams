#include "gpu_chams.hpp"
#include "gl_loader.hpp"
#include <iostream>
#include <cstring>

// Embedded GLSL Vertex Shader Source
static const char* vertex_shader_source = R"glsl(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in uvec4 aJoints;
layout(location = 4) in vec4 aWeights;

uniform mat4 uViewProj;
uniform mat4 uBones[128];
uniform float uGlowThickness;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    mat4 skin = uBones[aJoints.x] * aWeights.x
              + uBones[aJoints.y] * aWeights.y
              + uBones[aJoints.z] * aWeights.z
              + uBones[aJoints.w] * aWeights.w;

    vec4 worldPos = skin * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(skin) * aNormal;
    vUV = aUV;
    
    vec4 clipPos = uViewProj * worldPos;
    if (uGlowThickness > 0.0) {
        vec3 n = normalize(vNormal);
        vec4 clipN = uViewProj * (skin * vec4(aPos + n, 1.0));
        
        float invW0 = 1.0 / max(abs(clipPos.w), 1e-4);
        float invW1 = 1.0 / max(abs(clipN.w), 1e-4);
        vec2 ndc0 = clipPos.xy * invW0;
        vec2 ndc1 = clipN.xy * invW1;
        vec2 ndcDir = ndc1 - ndc0;
        float ndcLen = length(ndcDir);
        
        if (ndcLen > 1e-6) {
            ndcDir /= ndcLen;
            float ndcThickness = uGlowThickness * 0.0022;
            clipPos.xy += ndcDir * ndcThickness * clipPos.w;
        }
    }
    gl_Position = clipPos;
}
)glsl";

// Embedded GLSL Fragment Shader Source
static const char* fragment_shader_source = R"glsl(
#version 330 core
uniform vec4 uColor;
uniform int uStyle; 
uniform vec3 uCamPos;
uniform vec4 uGlowColor;
uniform float uGlowIntensity;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

out vec4 FragColor;

void main() {
    vec3 rawN = vNormal;
    float nLen = length(rawN);
    vec3 N = nLen > 0.0001 ? rawN / nLen : vec3(0.0, 1.0, 0.0);
    vec3 V = normalize(uCamPos - vWorldPos);
    float NdotV = max(dot(N, V), 0.0);
    float absViewDot = clamp(abs(dot(N, V)), 0.0, 1.0);
    
    vec3 finalColor = uColor.rgb;
    float finalAlpha = uColor.a;
    
    float rim = pow(1.0 - absViewDot, 2.0);
    
    // silhouetteAA: smooth edges at silhouette boundary
    float aaWidth = max(0.015, fwidth(absViewDot) * 4.0);
    float edgeAA = smoothstep(0.0, aaWidth, absViewDot);
    float silhouetteAA = 0.35 + 0.65 * edgeAA;

    if (uStyle == 1) { // Textured / View-space lit
        vec3 lightMain = normalize(vec3(0.3, 0.8, 0.6));
        vec3 lightFill = normalize(vec3(-0.2, -0.5, 0.4));
        float diffMain = max(0.0, dot(N, lightMain));
        float diffFill = max(0.0, dot(N, lightFill)) * 0.35;
        float ambient = 0.35;
        vec3 halfVec = normalize(lightMain + V);
        float spec = pow(max(dot(N, halfVec), 0.0), 24.0) * 0.4;
        float lighting = ambient + diffMain + diffFill + spec;
        vec3 rimColor = finalColor * 1.4;
        vec3 litColor = finalColor * lighting;
        vec3 result = mix(litColor, rimColor, rim * 0.4);
        FragColor = vec4(clamp(result, 0.0, 1.0), finalAlpha);
    } 
    else if (uStyle == 2) { // Flat
        FragColor = uColor;
    } 
    else if (uStyle == 3) { // Metallic
        vec3 darkColor = finalColor * 0.3;
        float reflection = 0.5 * N.y + 0.5;
        float specular = pow(absViewDot, 15.0);
        vec3 metalColor = mix(darkColor, finalColor, reflection);
        metalColor += vec3(1.0, 1.0, 1.0) * specular * 1.5;
        metalColor += finalColor * rim * 0.8;
        FragColor = vec4(clamp(metalColor, 0.0, 1.0), finalAlpha);
    } 
    else if (uStyle == 5) { // Glow Blend
        vec3 blendedColor = mix(finalColor, uGlowColor.rgb, rim);
        float blendedAlpha = mix(finalAlpha, uGlowColor.a, rim);
        FragColor = vec4(blendedColor * (1.0 + rim * 1.5), blendedAlpha);
    } 
    else if (uStyle == 6) { // CS2 Glow
        float rimGlow = pow(1.0 - absViewDot, 2.5);
        float rimSharp = pow(1.0 - absViewDot, 6.0);
        float rimSoft = pow(1.0 - absViewDot, 1.2);
        
        float core = clamp(rimSharp * 4.0, 0.0, 1.0);
        float mid = clamp(rimGlow * 2.2, 0.0, 1.0);
        float outer = clamp(rimSoft * 0.55, 0.0, 1.0);
        
        float bloom = core * 0.85 + mid * 0.55 + outer * 0.25;
        float alpha = clamp(bloom, 0.0, 1.0) * uGlowColor.a * silhouetteAA;
        
        vec3 glowRGB = uGlowColor.rgb * (1.0 + core * uGlowIntensity * 2.5 + mid * uGlowIntensity);
        FragColor = vec4(glowRGB, alpha);
    } 
    else {
        FragColor = uColor;
    }
}
)glsl";

GpuChamsRenderer::~GpuChamsRenderer() {
    cleanup();
}

void GpuChamsRenderer::cleanup() {
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

bool GpuChamsRenderer::compile_shader(unsigned int shader, const char* source) {
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetShaderInfoLog(shader, 512, nullptr, info_log);
        std::cerr << "GPU_CHAMS: Shader compilation error: " << info_log << std::endl;
        return false;
    }
    return true;
}

bool GpuChamsRenderer::link_program() {
    program_id = glCreateProgram();
    glAttachShader(program_id, vertex_shader);
    glAttachShader(program_id, fragment_shader);
    glLinkProgram(program_id);

    int success;
    glGetProgramiv(program_id, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(program_id, 512, nullptr, info_log);
        std::cerr << "GPU_CHAMS: Shader linking error: " << info_log << std::endl;
        return false;
    }
    return true;
}

bool GpuChamsRenderer::init() {
    vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    if (!compile_shader(vertex_shader, vertex_shader_source)) return false;

    fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    if (!compile_shader(fragment_shader, fragment_shader_source)) return false;

    if (!link_program()) return false;

    // Retrieve uniform locations
    loc_view_proj      = glGetUniformLocation(program_id, "uViewProj");
    loc_bones          = glGetUniformLocation(program_id, "uBones");
    loc_color          = glGetUniformLocation(program_id, "uColor");
    loc_style          = glGetUniformLocation(program_id, "uStyle");
    loc_cam_pos        = glGetUniformLocation(program_id, "uCamPos");
    loc_glow_color     = glGetUniformLocation(program_id, "uGlowColor");
    loc_glow_thickness = glGetUniformLocation(program_id, "uGlowThickness");
    loc_glow_intensity = glGetUniformLocation(program_id, "uGlowIntensity");

    return true;
}

void GpuChamsRenderer::begin(const float* view_proj, const float* cam_pos) {
    glUseProgram(program_id);
    
    // Upload view-projection matrix and camera position
    glUniformMatrix4fv(loc_view_proj, 1, GL_FALSE, view_proj);
    glUniform3fv(loc_cam_pos, 1, cam_pos);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void GpuChamsRenderer::render_mesh(unsigned int vao, unsigned int ibo, size_t index_count,
                                 const std::vector<source2::Mat3x4>& bones_palette,
                                 const float* color, int style,
                                 const float* glow_color,
                                 float glow_thickness,
                                 float glow_intensity) {
    // 1. Upload bones uniform array converting Mat3x4 -> Mat4
    float mat4_array[128 * 16];
    std::memset(mat4_array, 0, sizeof(mat4_array));

    for (size_t i = 0; i < bones_palette.size() && i < 128; ++i) {
        const auto& b = bones_palette[i];
        float* dest = &mat4_array[i * 16];
        // Column 0
        dest[0] = b.mat[0][0];
        dest[1] = b.mat[1][0];
        dest[2] = b.mat[2][0];
        dest[3] = 0.0f;
        // Column 1
        dest[4] = b.mat[0][1];
        dest[5] = b.mat[1][1];
        dest[6] = b.mat[2][1];
        dest[7] = 0.0f;
        // Column 2
        dest[8] = b.mat[0][2];
        dest[9] = b.mat[1][2];
        dest[10] = b.mat[2][2];
        dest[11] = 0.0f;
        // Column 3 (translation)
        dest[12] = b.mat[0][3];
        dest[13] = b.mat[1][3];
        dest[14] = b.mat[2][3];
        dest[15] = 1.0f;
    }
    // Fill remaining elements to identity
    for (size_t i = bones_palette.size(); i < 128; ++i) {
        float* dest = &mat4_array[i * 16];
        dest[0] = dest[5] = dest[10] = dest[15] = 1.0f;
    }

    glUniformMatrix4fv(loc_bones, 128, GL_FALSE, mat4_array);

    // 2. Upload style configuration and color
    glUniform4fv(loc_color, 1, color);
    glUniform1i(loc_style, style);

    float default_glow[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    const float* g_col = glow_color ? glow_color : default_glow;
    glUniform4fv(loc_glow_color, 1, g_col);
    glUniform1f(loc_glow_thickness, glow_thickness);
    glUniform1f(loc_glow_intensity, glow_intensity);

    // 3. Bind VAO and draw
    glBindVertexArray(vao);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glDrawElements(GL_TRIANGLES, (GLsizei)index_count, GL_UNSIGNED_INT, nullptr);
    
    glBindVertexArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

void GpuChamsRenderer::end() {
    glUseProgram(0);
}
