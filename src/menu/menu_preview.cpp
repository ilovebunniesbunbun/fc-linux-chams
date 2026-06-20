#include "menu_preview.hpp"

#include "renderer/gl_loader.hpp"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstring>
#include <vector>

#include "logger.hpp"
#include "model_cache.hpp"
#include "renderer/gl_loader.hpp"
#include "renderer/gpu_chams.hpp"
#include "overlay/esp_drawing.hpp"

// VPK Parser access for custom path
namespace AgentParser {
    void SetCustomVpkPath(const std::string& path);
}

// Player Preview Implementations
struct Vec3d {
    float x, y, z;
};

static inline Vec3d normalize_vec3d(Vec3d v)
{
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 0.00001f) {
        return {v.x / len, v.y / len, v.z / len};
    }
    return {0, 0, 0};
}

static inline Vec3d cross_vec3d(Vec3d a, Vec3d b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static void make_look_at(Vec3d eye, Vec3d center, Vec3d up, float* m)
{
    Vec3d f = normalize_vec3d({center.x - eye.x, center.y - eye.y, center.z - eye.z});
    Vec3d u = normalize_vec3d(up);
    Vec3d s = normalize_vec3d(cross_vec3d(f, u));
    u = cross_vec3d(s, f);

    m[0] = s.x;
    m[4] = s.y;
    m[8] = s.z;
    m[12] = -(s.x * eye.x + s.y * eye.y + s.z * eye.z);
    m[1] = u.x;
    m[5] = u.y;
    m[9] = u.z;
    m[13] = -(u.x * eye.x + u.y * eye.y + u.z * eye.z);
    m[2] = -f.x;
    m[6] = -f.y;
    m[10] = -f.z;
    m[14] = (f.x * eye.x + f.y * eye.y + f.z * eye.z);
    m[3] = 0.0f;
    m[7] = 0.0f;
    m[11] = 0.0f;
    m[15] = 1.0f;
}

static void make_perspective(float fovy, float aspect, float zNear, float zFar, float* m)
{
    float rad = fovy * 3.14159265f / 180.0f;
    float tanHalfFovy = std::tan(rad / 2.0f);

    std::memset(m, 0, sizeof(float) * 16);
    m[0] = 1.0f / (aspect * tanHalfFovy);
    m[5] = 1.0f / tanHalfFovy;
    m[10] = -(zFar + zNear) / (zFar - zNear);
    m[11] = -1.0f;
    m[14] = -(2.0f * zFar * zNear) / (zFar - zNear);
}

static void multiply_matrices(const float* a, const float* b, float* r)
{
    float temp[16];
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            temp[col * 4 + row] = sum;
        }
    }
    std::memcpy(r, temp, sizeof(float) * 16);
}

MenuPreview::~MenuPreview()
{
    cleanup();
}

void MenuPreview::init(const OverlayConfig& cfg)
{
    if (preview_initialized) return;

    // Set VPK path override
    AgentParser::SetCustomVpkPath(cfg.vpk_path);

    // Create FBO
    glGenFramebuffers(1, &preview_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, preview_fbo);

    // Create color texture
    glGenTextures(1, &preview_tex);
    glBindTexture(GL_TEXTURE_2D, preview_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, preview_w, preview_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, preview_tex, 0);

    // Create depth renderbuffer
    glGenRenderbuffers(1, &preview_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, preview_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, preview_w, preview_h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, preview_depth);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        FC2_LOG_ERROR("MENU_PREVIEW: Failed to create preview framebuffer");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Initialize sub-renderer and model cache
    preview_renderer = new GpuChamsRenderer();
    if (!preview_renderer->init()) {
        FC2_LOG_ERROR("MENU_PREVIEW: Failed to initialize preview chams renderer");
    }

    preview_model_cache = new ModelCache();

    if (!esp_renderer.init()) {
        FC2_LOG_ERROR("MENU_PREVIEW: Failed to initialize preview ESP renderer");
    }

    preview_initialized = true;
}

void MenuPreview::cleanup()
{
    if (!preview_initialized) return;

    if (preview_fbo) {
        glDeleteFramebuffers(1, &preview_fbo);
        preview_fbo = 0;
    }
    if (preview_tex) {
        glDeleteTextures(1, &preview_tex);
        preview_tex = 0;
    }
    if (preview_depth) {
        glDeleteRenderbuffers(1, &preview_depth);
        preview_depth = 0;
    }

    if (preview_renderer) {
        delete preview_renderer;
        preview_renderer = nullptr;
    }

    if (preview_model_cache) {
        delete preview_model_cache;
        preview_model_cache = nullptr;
    }

    esp_renderer.cleanup();

    preview_initialized = false;
}

void MenuPreview::render(OverlayConfig& cfg, float rotation_deg)
{
    if (!preview_initialized) return;

    // Synchronize VPK path override
    AgentParser::SetCustomVpkPath(cfg.vpk_path);

    const CachedModel* model = preview_model_cache->get_or_load("agents/models/ctm_sas/ctm_sas.vmdl");
    if (!model || !model->valid) {
        glBindFramebuffer(GL_FRAMEBUFFER, preview_fbo);
        glViewport(0, 0, preview_w, preview_h);
        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, preview_fbo);
    glViewport(0, 0, preview_w, preview_h);

    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float rad_angle = rotation_deg * 3.14159265f / 180.0f;
    float dist = 105.0f;
    Vec3d eye = {dist * std::cos(rad_angle), dist * std::sin(rad_angle), 39.2f};
    Vec3d center = {0.0f, 0.0f, 39.2f};
    Vec3d up = {0.0f, 0.0f, 1.0f};

    float view[16];
    make_look_at(eye, center, up, view);

    float proj[16];
    make_perspective(45.0f, (float)preview_w / preview_h, 0.1f, 1000.0f, proj);

    float view_proj[16];
    multiply_matrices(proj, view, view_proj);

    float gl_vp[16];
    std::memcpy(gl_vp, view_proj, sizeof(float) * 16);

    float row_major_vp[16];
    for (int c = 0; c < 4; ++c) {
        row_major_vp[0 * 4 + c] = view_proj[c * 4 + 0];
        row_major_vp[1 * 4 + c] = view_proj[c * 4 + 1];
        row_major_vp[2 * 4 + c] = view_proj[c * 4 + 2];
        row_major_vp[3 * 4 + c] = view_proj[c * 4 + 3];
    }

    float cam_pos[3] = {eye.x, eye.y, eye.z};

    std::vector<source2::Mat3x4> identity_bones(model->mesh.bone_count);
    for (int j = 0; j < model->mesh.bone_count; ++j) {
        identity_bones[j] = source2::Mat3x4(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    }

    int style_vis_id = get_style_id(cfg.style_vis);

    // Calculate 3D bind-pose bone positions if ESP is enabled
    std::vector<Vec3> preview_bones;
    std::vector<float> dummy_vis;
    if (cfg.esp_enabled) {
        preview_bones.resize(model->mesh.bone_count);
        for (int j = 0; j < model->mesh.bone_count; ++j) {
            const auto& ibp = model->mesh.inv_bind_poses[j];
            float px = -(ibp[0][0] * ibp[0][3] + ibp[1][0] * ibp[1][3] + ibp[2][0] * ibp[2][3]);
            float py = -(ibp[0][1] * ibp[0][3] + ibp[1][1] * ibp[1][3] + ibp[2][1] * ibp[2][3]);
            float pz = -(ibp[0][2] * ibp[0][3] + ibp[1][2] * ibp[1][3] + ibp[2][2] * ibp[2][3]);
            preview_bones[j] = {px, py, pz};
        }
        dummy_vis.assign(128, 1.0f);
    }

    // 1. Draw Glow Pass (Chams glow and/or Skeleton glow)
    bool draw_glow = (cfg.glow_enabled && cfg.outline_mode == "glow") ||
                     (cfg.esp_enabled && cfg.esp_skeleton && cfg.esp_skeleton_glow);
    if (draw_glow) {
        preview_renderer->begin_glow_pass(preview_w, preview_h, gl_vp, cam_pos);
        if (cfg.glow_enabled && cfg.outline_mode == "glow") {
            preview_renderer->render_glow_silhouette(model->vao, model->ibo, model->index_count, identity_bones,
                                                     cfg.glow_color);
        }

        if (cfg.esp_enabled && cfg.esp_skeleton && cfg.esp_skeleton_glow) {
            GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
            glDrawBuffers(1, &drawBuffer);

            esp_renderer.clear();
            esp_renderer.set_projection(gl_vp);

            float override_glow_color[4];
            if (cfg.esp_skeleton_glow_health_based) {
                float hp_pct = 1.0f;  // 100% health in preview
                for (int c = 0; c < 4; ++c) {
                    override_glow_color[c] = cfg.esp_skeleton_glow_health_start[c] * hp_pct +
                                             cfg.esp_skeleton_glow_health_end[c] * (1.0f - hp_pct);
                }
            } else {
                std::memcpy(override_glow_color, cfg.esp_skeleton_glow_color, sizeof(float) * 4);
            }

            float pulse_factor = 1.0f;
            if (cfg.esp_skeleton_glow_pulse) {
                float time = static_cast<float>(glfwGetTime());
                pulse_factor = 0.625f + 0.375f * std::sin(time * cfg.esp_skeleton_glow_pulse_speed);
            }

            esp_renderer.add_skeleton_3d(preview_bones, 0, dummy_vis, cfg, true, pulse_factor, override_glow_color);
            esp_renderer.flush_lines();

            GLenum drawBuffers[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
            glDrawBuffers(2, drawBuffers);
        }

        // Determine glow parameters for compositing
        float glow_thick = cfg.glow_thickness;
        float glow_int = cfg.glow_intensity;
        bool pulse = cfg.glow_pulse;
        float speed = cfg.glow_pulse_speed;

        if (!(cfg.glow_enabled && cfg.outline_mode == "glow") && cfg.esp_skeleton_glow) {
            glow_thick = cfg.esp_skeleton_glow_thickness;
            glow_int = cfg.esp_skeleton_glow_intensity;
            pulse = cfg.esp_skeleton_glow_pulse;
            speed = cfg.esp_skeleton_glow_pulse_speed;
        }

        float current_intensity = glow_int;
        if (pulse) {
            float time = static_cast<float>(glfwGetTime());
            float factor = 0.625f + 0.375f * std::sin(time * speed);
            current_intensity *= factor;
        }

        preview_renderer->end_glow_pass(preview_w, preview_h, glow_thick, current_intensity, preview_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, preview_fbo);
    }

    // 2. Draw Body Pass (Chams)
    bool is_stencil_outline = cfg.glow_enabled && cfg.outline_mode == "stencil";
    if (style_vis_id > 0 || is_stencil_outline) {
        float preview_glow_intensity = cfg.glow_intensity;
        if (cfg.glow_pulse) {
            float time = static_cast<float>(glfwGetTime());
            float factor = 0.625f + 0.375f * std::sin(time * cfg.glow_pulse_speed);
            preview_glow_intensity *= factor;
        }
        preview_renderer->begin_body_pass(gl_vp, cam_pos, cfg.flat_chams_no_overlap || is_stencil_outline);
        preview_renderer->render_mesh(model->vao, model->ibo, model->index_count, identity_bones, cfg.color_vis,
                                      style_vis_id, is_stencil_outline ? cfg.glow_color : cfg.color_vis_sec,
                                      is_stencil_outline ? cfg.glow_thickness : 0.0f, preview_glow_intensity, 0.0f,
                                      cfg.flat_chams_no_overlap, -1, is_stencil_outline);
        preview_renderer->end_body_pass();
    }

    // 3. Draw 3D Skeleton
    if (cfg.esp_enabled && cfg.esp_skeleton) {
        esp_renderer.clear();
        esp_renderer.set_projection(gl_vp);
        esp_renderer.add_skeleton_3d(preview_bones, 0, dummy_vis, cfg, false, 1.0f, nullptr);
        esp_renderer.flush_lines();
    }

    // 4. Draw 2D Box & Health Bar
    if (cfg.esp_enabled && (cfg.esp_box || cfg.esp_health_bar)) {
        glDisable(GL_DEPTH_TEST);
        esp_renderer.clear();
        esp_renderer.set_ortho(0, preview_w, preview_h, 0);

        float bx, by, bw, bh;
        Vec3 origin = {0.0f, 0.0f, 0.0f};
        Vec3 cam_pos_vec = {eye.x, eye.y, eye.z};

        if (get_player_bounds(preview_bones, origin, cam_pos_vec, row_major_vp, preview_w, preview_h, bx, by, bw, bh,
                              cfg)) {
            if (cfg.esp_box) {
                esp_renderer.add_outlined_rect_2d(bx, by, bw, bh, cfg.esp_box_color, cfg.esp_box_thickness,
                                                  cfg.esp_box_outline);
            }
            if (cfg.esp_health_bar) {
                esp_renderer.add_health_bar_2d(bx, by, bw, bh, 100.0f, cfg);  // Rendered at 100% health in preview
            }
        }

        esp_renderer.flush_triangles();
        esp_renderer.flush_lines();
        glEnable(GL_DEPTH_TEST);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
