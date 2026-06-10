#include "gpu_chams.hpp"
#include "gl_loader.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>

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

    vec3 skinnedNormal = normalize(mat3(skin) * aNormal);
    vec4 worldPos = skin * vec4(aPos, 1.0);
    
    if (uGlowThickness > 0.0) {
        // Expand the model uniformly in world space along the skinned normals
        worldPos.xyz += skinnedNormal * uGlowThickness * 0.45;
    }
    
    vWorldPos = worldPos.xyz;
    vNormal = skinnedNormal;
    vUV = aUV;
    
    gl_Position = uViewProj * worldPos;
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
uniform float uGlowBlur;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

layout(location = 0) out vec4 FragColor;
layout(location = 1) out vec4 MaskColor;

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

    MaskColor = vec4(0.0);

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
        MaskColor = vec4(1.0, 1.0, 1.0, 1.0);
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
    else if (uStyle == 6) { // CS2 Glow (Fresnel Xray)
        float rimGlow = pow(1.0 - absViewDot, 2.5);
        float rimSharp = pow(1.0 - absViewDot, 6.0);
        float rimSoft = pow(1.0 - absViewDot, 1.2);
        
        float core = clamp(rimSharp * 4.0, 0.0, 1.0);
        float mid = clamp(rimGlow * 2.2, 0.0, 1.0);
        float outer = clamp(rimSoft * 0.55, 0.0, 1.0);
        
        float bloom = core * 0.85 + mid * 0.55 + outer * 0.25;
        float edgeAlpha = clamp(bloom, 0.0, 1.0) * silhouetteAA;
        
        vec3 baseColor = mix(uColor.rgb, uGlowColor.rgb, bloom);
        float alpha = mix(uColor.a, uGlowColor.a, bloom) * edgeAlpha;
        
        vec3 glowRGB = baseColor * (1.0 + core * uGlowIntensity * 2.5 + mid * uGlowIntensity);
        FragColor = vec4(glowRGB, alpha);
    } 
    else if (uStyle == 7) { // Outer Glow Shell
        float factor = 1.0;
        if (uGlowBlur > 0.0) {
            factor = clamp(absViewDot / uGlowBlur, 0.0, 1.0);
            factor = factor * factor * (3.0 - 2.0 * factor); // smoothstep
        }
        float alpha = factor * uGlowColor.a * silhouetteAA;
        vec3 glowRGB = uGlowColor.rgb * uGlowIntensity;
        FragColor = vec4(glowRGB, alpha);
    }
    else {
        FragColor = uColor;
    }
}
)glsl";

// Embedded GLSL Blur Shaders
static const char* blur_vertex_shader_source = R"glsl(
#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vUV;
void main() {
    vUV = aPos * 0.5 + vec2(0.5);
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)glsl";

static const char* blur_fragment_shader_source = R"glsl(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform vec2 uDir;

const float weights[7] = float[](0.198596, 0.175713, 0.121703, 0.065984, 0.027839, 0.009246, 0.002403);

void main() {
    vec4 texColor = texture(uTexture, vUV);
    vec4 color = texColor * weights[0];
    for (int i = 1; i < 7; ++i) {
        color += texture(uTexture, vUV + float(i) * uDir) * weights[i];
        color += texture(uTexture, vUV - float(i) * uDir) * weights[i];
    }
    FragColor = color;
}
)glsl";

// Embedded GLSL Sobel Seed Shader Source
static const char* seed_fragment_shader_source = R"glsl(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform vec2 uDir;

void main() {
    vec2 uv = vUV;
    vec2 px = uDir;

    float mM  = texture(uTexture, uv).a;
    float mN  = texture(uTexture, uv + vec2(0.0, -px.y)).a;
    float mS  = texture(uTexture, uv + vec2(0.0,  px.y)).a;
    float mW  = texture(uTexture, uv + vec2(-px.x, 0.0)).a;
    float mE  = texture(uTexture, uv + vec2( px.x, 0.0)).a;
    float mNW = texture(uTexture, uv + vec2(-px.x, -px.y)).a;
    float mNE = texture(uTexture, uv + vec2( px.x, -px.y)).a;
    float mSW = texture(uTexture, uv + vec2(-px.x,  px.y)).a;
    float mSE = texture(uTexture, uv + vec2( px.x,  px.y)).a;

    float gX = -mNW - 2.0 * mW - mSW + mNE + 2.0 * mE + mSE;
    float gY = -mNW - 2.0 * mN - mNE + mSW + 2.0 * mS + mSE;
    float grad = sqrt(gX*gX + gY*gY);

    float edge = clamp((grad - 0.001) * 100.0, 0.0, 1.0);
    edge = edge * edge * (3.0 - 2.0 * edge);

    if (edge < 0.001) {
        FragColor = vec4(0.0);
        return;
    }

    vec4 centerTex = texture(uTexture, uv);
    vec3 tint = centerTex.rgb;
    float intensity = centerTex.a;
    float e = edge * intensity;
    FragColor = vec4(tint * e, e);
}
)glsl";

// Embedded GLSL Masked Composite Shader Source
static const char* composite_fragment_shader_source = R"glsl(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uGlowTex;
uniform sampler2D uMaskTex;
uniform vec2 uDir;
uniform float uIntensity;

void main() {
    vec2 px = abs(uDir);
    vec2 uv = vUV;

    vec4 glow = texture(uGlowTex, uv);
    vec4 mask = texture(uMaskTex, uv);
    float maskA = max(max(mask.r, mask.g), max(mask.b, mask.a));
    float outside = clamp(1.0 - maskA, 0.0, 1.0);
    float keep = outside * outside * (3.0 - 2.0 * outside);

    float aM = glow.a;
    if (aM <= 0.001) {
        FragColor = vec4(0.0);
        return;
    }

    float a = max(aM, 1e-4);
    vec3 base = glow.rgb / a;

    if (aM < 0.999) {
        float aN = texture(uGlowTex, uv + vec2(0.0, -px.y)).a;
        float aS = texture(uGlowTex, uv + vec2(0.0,  px.y)).a;
        float aW = texture(uGlowTex, uv + vec2(-px.x, 0.0)).a;
        float aE = texture(uGlowTex, uv + vec2( px.x, 0.0)).a;
        float aMin = min(aM, min(min(aN, aS), min(aW, aE)));
        float aMax = max(aM, max(max(aN, aS), max(aW, aE)));
        float aRange = aMax - aMin;
        float aAvg = (aM + aN + aS + aW + aE) * 0.2;
        float edge = clamp((aRange - 0.002) * 140.0, 0.0, 1.0);
        aM = mix(aM, aAvg, edge * 0.9);
    }

    float t = clamp(aM * uIntensity, 0.0, 1.0) * keep;
    FragColor = vec4(base, t);
}
)glsl";

GpuChamsRenderer::~GpuChamsRenderer() {
    cleanup();
}

void GpuChamsRenderer::cleanup() {
    cleanup_fbos();
    if (quad_vao) {
        glDeleteVertexArrays(1, &quad_vao);
        quad_vao = 0;
    }
    if (quad_vbo) {
        glDeleteBuffers(1, &quad_vbo);
        quad_vbo = 0;
    }
    if (blur_program) {
        glDeleteProgram(blur_program);
        blur_program = 0;
    }
    if (blur_vs) {
        glDeleteShader(blur_vs);
        blur_vs = 0;
    }
    if (blur_fs) {
        glDeleteShader(blur_fs);
        blur_fs = 0;
    }
    if (seed_program) {
        glDeleteProgram(seed_program);
        seed_program = 0;
    }
    if (seed_fs) {
        glDeleteShader(seed_fs);
        seed_fs = 0;
    }
    if (composite_program) {
        glDeleteProgram(composite_program);
        composite_program = 0;
    }
    if (composite_fs) {
        glDeleteShader(composite_fs);
        composite_fs = 0;
    }

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
    loc_glow_blur      = glGetUniformLocation(program_id, "uGlowBlur");

    if (!init_blur_shader()) return false;
    if (!init_seed_shader(blur_vs)) return false;
    if (!init_composite_shader(blur_vs)) return false;

    // Setup quad VAO/VBO
    float quadVertices[] = {
        -1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f, -1.0f,

        -1.0f,  1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f
    };

    glGenVertexArrays(1, &quad_vao);
    glGenBuffers(1, &quad_vbo);

    glBindVertexArray(quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

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
                                 float glow_intensity,
                                 float glow_blur) {
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
    glUniform1f(loc_glow_blur, glow_blur);

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

bool GpuChamsRenderer::init_blur_shader() {
    blur_vs = glCreateShader(GL_VERTEX_SHADER);
    if (!compile_shader(blur_vs, blur_vertex_shader_source)) return false;

    blur_fs = glCreateShader(GL_FRAGMENT_SHADER);
    if (!compile_shader(blur_fs, blur_fragment_shader_source)) return false;

    blur_program = glCreateProgram();
    glAttachShader(blur_program, blur_vs);
    glAttachShader(blur_program, blur_fs);
    glLinkProgram(blur_program);

    int success;
    glGetProgramiv(blur_program, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(blur_program, 512, nullptr, info_log);
        std::cerr << "GPU_CHAMS: Blur shader linking error: " << info_log << std::endl;
        return false;
    }

    loc_blur_texture = glGetUniformLocation(blur_program, "uTexture");
    loc_blur_dir = glGetUniformLocation(blur_program, "uDir");

    return true;
}

bool GpuChamsRenderer::init_seed_shader(unsigned int vs) {
    seed_fs = glCreateShader(GL_FRAGMENT_SHADER);
    if (!compile_shader(seed_fs, seed_fragment_shader_source)) return false;

    seed_program = glCreateProgram();
    glAttachShader(seed_program, vs);
    glAttachShader(seed_program, seed_fs);
    glLinkProgram(seed_program);

    int success;
    glGetProgramiv(seed_program, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(seed_program, 512, nullptr, info_log);
        std::cerr << "GPU_CHAMS: Seed shader linking error: " << info_log << std::endl;
        return false;
    }

    loc_seed_texture = glGetUniformLocation(seed_program, "uTexture");
    loc_seed_dir = glGetUniformLocation(seed_program, "uDir");
    return true;
}

bool GpuChamsRenderer::init_composite_shader(unsigned int vs) {
    composite_fs = glCreateShader(GL_FRAGMENT_SHADER);
    if (!compile_shader(composite_fs, composite_fragment_shader_source)) return false;

    composite_program = glCreateProgram();
    glAttachShader(composite_program, vs);
    glAttachShader(composite_program, composite_fs);
    glLinkProgram(composite_program);

    int success;
    glGetProgramiv(composite_program, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[512];
        glGetProgramInfoLog(composite_program, 512, nullptr, info_log);
        std::cerr << "GPU_CHAMS: Composite shader linking error: " << info_log << std::endl;
        return false;
    }

    loc_composite_glow_tex = glGetUniformLocation(composite_program, "uGlowTex");
    loc_composite_mask_tex = glGetUniformLocation(composite_program, "uMaskTex");
    loc_composite_dir = glGetUniformLocation(composite_program, "uDir");
    loc_composite_intensity = glGetUniformLocation(composite_program, "uIntensity");
    return true;
}

void GpuChamsRenderer::cleanup_fbos() {
    if (glow_fbo_silhouette) {
        glDeleteFramebuffers(1, &glow_fbo_silhouette);
        glow_fbo_silhouette = 0;
    }
    if (glow_tex_silhouette) {
        glDeleteTextures(1, &glow_tex_silhouette);
        glow_tex_silhouette = 0;
    }
    if (glow_fbo_mask) {
        glDeleteFramebuffers(1, &glow_fbo_mask);
        glow_fbo_mask = 0;
    }
    if (glow_tex_mask) {
        glDeleteTextures(1, &glow_tex_mask);
        glow_tex_mask = 0;
    }
    if (glow_fbo_a) {
        glDeleteFramebuffers(1, &glow_fbo_a);
        glow_fbo_a = 0;
    }
    if (glow_tex_a) {
        glDeleteTextures(1, &glow_tex_a);
        glow_tex_a = 0;
    }
    if (glow_fbo_b) {
        glDeleteFramebuffers(1, &glow_fbo_b);
        glow_fbo_b = 0;
    }
    if (glow_tex_b) {
        glDeleteTextures(1, &glow_tex_b);
        glow_tex_b = 0;
    }
    glow_last_w = 0;
    glow_last_h = 0;
}

void GpuChamsRenderer::update_fbos(int width, int height) {
    if (width == glow_last_w && height == glow_last_h) {
        return;
    }

    cleanup_fbos();

    int fbo_w = (width + 1) / 2;
    int fbo_h = (height + 1) / 2;
    if (fbo_w < 1) fbo_w = 1;
    if (fbo_h < 1) fbo_h = 1;

    // Create glow_fbo_silhouette with two attachments (MRT)
    glGenFramebuffers(1, &glow_fbo_silhouette);
    glBindFramebuffer(GL_FRAMEBUFFER, glow_fbo_silhouette);

    // 1. Silhouette texture
    glGenTextures(1, &glow_tex_silhouette);
    glBindTexture(GL_TEXTURE_2D, glow_tex_silhouette);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glow_tex_silhouette, 0);

    // 2. Mask texture
    glGenTextures(1, &glow_tex_mask);
    glBindTexture(GL_TEXTURE_2D, glow_tex_mask);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, glow_tex_mask, 0);

    // Set draw buffers for MRT
    GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, drawBuffers);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "GPU_CHAMS: Failed to create glow_fbo_silhouette" << std::endl;
    }

    // Create glow_fbo_a & glow_tex_a at half resolution (width/2 x height/2)
    glGenFramebuffers(1, &glow_fbo_a);
    glBindFramebuffer(GL_FRAMEBUFFER, glow_fbo_a);

    glGenTextures(1, &glow_tex_a);
    glBindTexture(GL_TEXTURE_2D, glow_tex_a);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbo_w, fbo_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glow_tex_a, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "GPU_CHAMS: Failed to create glow_fbo_a" << std::endl;
    }

    // Create glow_fbo_b & glow_tex_b at half resolution (width/2 x height/2)
    glGenFramebuffers(1, &glow_fbo_b);
    glBindFramebuffer(GL_FRAMEBUFFER, glow_fbo_b);

    glGenTextures(1, &glow_tex_b);
    glBindTexture(GL_TEXTURE_2D, glow_tex_b);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbo_w, fbo_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glow_tex_b, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "GPU_CHAMS: Failed to create glow_fbo_b" << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glow_last_w = width;
    glow_last_h = height;
}

void GpuChamsRenderer::begin_glow_pass(int width, int height, const float* view_proj, const float* cam_pos) {
    update_fbos(width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, glow_fbo_silhouette);
    GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, drawBuffers);

    glViewport(0, 0, width, height); // Render silhouette at full resolution
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program_id);
    glUniformMatrix4fv(loc_view_proj, 1, GL_FALSE, view_proj);
    glUniform3fv(loc_cam_pos, 1, cam_pos);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void GpuChamsRenderer::render_glow_silhouette(unsigned int vao, unsigned int ibo, size_t index_count,
                                             const std::vector<source2::Mat3x4>& bones_palette,
                                             const float* color) {
    render_mesh(vao, ibo, index_count, bones_palette, color, 2);
}

void GpuChamsRenderer::end_glow_pass(int width, int height, float thickness, float intensity, unsigned int target_fbo) {
    int fbo_w = (width + 1) / 2;
    int fbo_h = (height + 1) / 2;
    if (fbo_w < 1) fbo_w = 1;
    if (fbo_h < 1) fbo_h = 1;

    // Disable blending during post-process passes to prevent trails/accumulation
    glDisable(GL_BLEND);

    // 1. Sobel Seed Pass: silhouette_tex -> glow_tex_a (half resolution)
    glBindFramebuffer(GL_FRAMEBUFFER, glow_fbo_a);
    glViewport(0, 0, fbo_w, fbo_h);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(seed_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, glow_tex_silhouette);
    glUniform1i(loc_seed_texture, 0);
    glUniform2f(loc_seed_dir, 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));

    glBindVertexArray(quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Calculate kernel scale (matching vpk-parser)
    float base_kernel = 0.95f + thickness * 0.55f;
    float kernel_scale = std::min(2.6f, std::max(0.5f, base_kernel));

    float dx = 1.0f / static_cast<float>(fbo_w);
    float dy = 1.0f / static_cast<float>(fbo_h);

    glUseProgram(blur_program);
    glActiveTexture(GL_TEXTURE0);

    // 2. Blur H (1.0x): glow_tex_a -> glow_tex_b
    glBindFramebuffer(GL_FRAMEBUFFER, glow_fbo_b);
    glBindTexture(GL_TEXTURE_2D, glow_tex_a);
    glUniform1i(loc_blur_texture, 0);
    glUniform2f(loc_blur_dir, dx * kernel_scale, 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 3. Blur V (1.0x): glow_tex_b -> glow_tex_a
    glBindFramebuffer(GL_FRAMEBUFFER, glow_fbo_a);
    glBindTexture(GL_TEXTURE_2D, glow_tex_b);
    glUniform1i(loc_blur_texture, 0);
    glUniform2f(loc_blur_dir, 0.0f, dy * kernel_scale);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 4. Blur H (0.7x): glow_tex_a -> glow_tex_b
    glBindFramebuffer(GL_FRAMEBUFFER, glow_fbo_b);
    glBindTexture(GL_TEXTURE_2D, glow_tex_a);
    glUniform1i(loc_blur_texture, 0);
    glUniform2f(loc_blur_dir, dx * kernel_scale * 0.7f, 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 5. Blur V (0.7x): glow_tex_b -> glow_tex_a
    glBindFramebuffer(GL_FRAMEBUFFER, glow_fbo_a);
    glBindTexture(GL_TEXTURE_2D, glow_tex_b);
    glUniform1i(loc_blur_texture, 0);
    glUniform2f(loc_blur_dir, 0.0f, dy * kernel_scale * 0.7f);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 6. Masked Composite: glow_tex_a + mask_tex -> screen (additive blend)
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(0, 0, width, height);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Additive blending!
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glUseProgram(composite_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, glow_tex_a);
    glUniform1i(loc_composite_glow_tex, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, glow_tex_mask);
    glUniform1i(loc_composite_mask_tex, 1);

    glUniform2f(loc_composite_dir, dx, dy);

    // Compute intensity formula on CPU (matching vpk-parser)
    float comp_intensity = std::min(9.0f, std::max(4.5f, 4.5f + thickness * 1.0f)) * std::max(0.25f, intensity);
    glUniform1f(loc_composite_intensity, comp_intensity);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Clean up bindings
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    // Restore standard states
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
}

void GpuChamsRenderer::begin_body_pass(const float* view_proj, const float* cam_pos) {
    glUseProgram(program_id);
    glUniformMatrix4fv(loc_view_proj, 1, GL_FALSE, view_proj);
    glUniform3fv(loc_cam_pos, 1, cam_pos);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

void GpuChamsRenderer::end_body_pass() {
    glDisable(GL_CULL_FACE);
    glUseProgram(0);
}

