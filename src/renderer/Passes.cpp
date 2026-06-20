#include "Passes.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <cstring>
#include <algorithm>
#include <cmath>

void ChamsPass::run(const ChamsPassInputs& inputs) {
    const auto& input = inputs.input;
    const auto& state = inputs.state;
    const auto& cfg = inputs.cfg;
    auto& chams_renderer = inputs.chams_renderer;
    auto& depth_prepass = inputs.depth_prepass;
    auto& esp_renderer = inputs.esp_renderer;
    int window_width = inputs.window_width;
    int window_height = inputs.window_height;

    // 1. Run Map Depth Prepass (if active and loaded)
    bool has_prepass = cfg.use_depth_prepass && depth_prepass.has_geometry();
    if (has_prepass) {
        if (inputs.profiler) inputs.profiler->begin_section(GpuTimerSection::DEPTH_PREPASS);
        depth_prepass.render(input.gl_vp);
        if (inputs.profiler) inputs.profiler->end_section(GpuTimerSection::DEPTH_PREPASS);
    }

    // Resolve styles in real-time
    int style_vis_id = get_style_id(cfg.style_vis);
    int style_invis_id = get_style_id(cfg.style_invis);

    // 2. Render Player Glow / Skeleton Silhouette
    bool run_glow = (cfg.glow_enabled && cfg.outline_mode == "glow") || (cfg.esp_enabled && cfg.esp_skeleton && cfg.esp_skeleton_glow);
    if (run_glow) {
        if (inputs.profiler) inputs.profiler->begin_section(GpuTimerSection::GLOW_SILHOUETTE);
        chams_renderer.begin_glow_pass(window_width, window_height, input.gl_vp, input.cam_pos_arr);
        
        if (cfg.glow_enabled && cfg.outline_mode == "glow") {
            size_t idx = 0;
            while (idx < state.sorted_player_indices.size()) {
                int first_player_idx = state.sorted_player_indices[idx];
                const auto& first_rp = state.render_palettes[first_player_idx];
                
                if (!(first_rp.is_visible || cfg.show_invisible)) {
                    idx++;
                    continue;
                }

                unsigned int batch_vao = first_rp.model->vao;
                unsigned int batch_ibo = first_rp.model->ibo;
                size_t batch_index_count = first_rp.model->index_count;

                int ubo_slots[8];
                float colors[8 * 4];
                int count = 0;
                int base_slot = -1;

                size_t next_idx = idx;
                while (next_idx < state.sorted_player_indices.size() && count < 8) {
                    int p_idx = state.sorted_player_indices[next_idx];
                    const auto& rp = state.render_palettes[p_idx];

                    if (rp.model->vao != batch_vao) {
                        break;
                    }

                    if (!(rp.is_visible || cfg.show_invisible)) {
                        next_idx++;
                        continue;
                    }

                    int slot = state.player_ubo_slots[p_idx];
                    if (base_slot < 0) {
                        base_slot = slot;
                    }

                    int current_min = base_slot;
                    int current_max = base_slot;
                    for (int k = 0; k < count; ++k) {
                        if (ubo_slots[k] < current_min) current_min = ubo_slots[k];
                        if (ubo_slots[k] > current_max) current_max = ubo_slots[k];
                    }
                    int next_min = std::min(current_min, slot);
                    int next_max = std::max(current_max, slot);
                    if (next_max - next_min >= 8) {
                        break;
                    }

                    ubo_slots[count] = slot;

                    float custom_color[4];
                    if (cfg.glow_health_based) {
                        float hp_factor = static_cast<float>(input.packet.players[p_idx].health) / 100.0f;
                        if (hp_factor < 0.0f) hp_factor = 0.0f;
                        if (hp_factor > 1.0f) hp_factor = 1.0f;
                        for (int c = 0; c < 4; ++c) {
                            custom_color[c] = cfg.glow_health_end[c] + hp_factor * (cfg.glow_health_start[c] - cfg.glow_health_end[c]);
                        }
                    } else {
                        std::memcpy(custom_color, cfg.glow_color, sizeof(float) * 4);
                    }
                    std::memcpy(&colors[count * 4], custom_color, sizeof(float) * 4);

                    count++;
                    next_idx++;
                }

                if (count > 0) {
                    chams_renderer.render_glow_silhouette_instanced(batch_vao, batch_ibo, batch_index_count,
                                                                    ubo_slots, colors, count);
                }
                idx = next_idx;
            }
        }

        if (cfg.esp_enabled && cfg.esp_skeleton && cfg.esp_skeleton_glow) {
            GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
            glDrawBuffers(1, &drawBuffer);

            esp_renderer.clear();
            esp_renderer.set_projection(input.gl_vp);

            float pulse_factor = 1.0f;
            if (cfg.esp_skeleton_glow_pulse) {
                float time = static_cast<float>(glfwGetTime());
                pulse_factor = 0.625f + 0.375f * std::sin(time * cfg.esp_skeleton_glow_pulse_speed);
            }

            for (int i = 0; i < input.packet.player_count; ++i) {
                const auto& rp = state.render_palettes[i];
                if (!rp.model) continue;

                float custom_glow_color[4];
                const float* glow_color_ptr = nullptr;
                if (cfg.esp_skeleton_glow_health_based) {
                    float hp_factor = static_cast<float>(input.packet.players[i].health) / 100.0f;
                    if (hp_factor < 0.0f) hp_factor = 0.0f;
                    if (hp_factor > 1.0f) hp_factor = 1.0f;

                    for (int c = 0; c < 4; ++c) {
                        custom_glow_color[c] = cfg.esp_skeleton_glow_health_end[c] + hp_factor * (cfg.esp_skeleton_glow_health_start[c] - cfg.esp_skeleton_glow_health_end[c]);
                    }
                    glow_color_ptr = custom_glow_color;
                }

                esp_renderer.add_skeleton_3d(rp.sanitized_bones, i, state.dummy_vis, cfg, true, pulse_factor, glow_color_ptr);
            }

            esp_renderer.flush_lines();

            GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
            glDrawBuffers(2, drawBuffers);
        }

        float current_thickness = cfg.glow_enabled ? cfg.glow_thickness : cfg.esp_skeleton_glow_thickness;
        float current_intensity = cfg.glow_enabled ? cfg.glow_intensity : cfg.esp_skeleton_glow_intensity;
        bool should_pulse = cfg.glow_enabled ? cfg.glow_pulse : cfg.esp_skeleton_glow_pulse;
        float pulse_speed = cfg.glow_enabled ? cfg.glow_pulse_speed : cfg.esp_skeleton_glow_pulse_speed;

        if (should_pulse) {
            float time = static_cast<float>(glfwGetTime());
            float factor = 0.625f + 0.375f * std::sin(time * pulse_speed);
            current_intensity *= factor;
        }
        if (inputs.profiler) inputs.profiler->end_section(GpuTimerSection::GLOW_SILHOUETTE);

        if (inputs.profiler) inputs.profiler->begin_section(GpuTimerSection::GLOW_POSTPROCESS);
        chams_renderer.end_glow_pass(window_width, window_height, current_thickness, current_intensity);
        if (inputs.profiler) inputs.profiler->end_section(GpuTimerSection::GLOW_POSTPROCESS);
    }

    // 3. Body Pass
    float current_glow_intensity = cfg.glow_intensity;
    if (cfg.glow_pulse) {
        float time = static_cast<float>(glfwGetTime());
        float factor = 0.625f + 0.375f * std::sin(time * cfg.glow_pulse_speed);
        current_glow_intensity *= factor;
    }
    
    bool is_stencil_outline = cfg.glow_enabled && cfg.outline_mode == "stencil";
    bool needs_stencil = cfg.flat_chams_no_overlap || is_stencil_outline;

    if (inputs.profiler) inputs.profiler->begin_section(GpuTimerSection::BODY_PASS);

    chams_renderer.begin_body_pass(input.gl_vp, input.cam_pos_arr, needs_stencil);

    auto draw_player_batches = [&](const std::vector<int>& target_indices, int style_id, const float* primary_color, const float* secondary_color) {
        if (target_indices.empty()) return;
        if (style_id <= 0 && !(cfg.glow_enabled && cfg.outline_mode == "stencil")) return;

        size_t idx = 0;
        while (idx < target_indices.size()) {
            int first_player_idx = target_indices[idx];
            const auto& first_rp = state.render_palettes[first_player_idx];

            unsigned int batch_vao = first_rp.model->vao;
            unsigned int batch_ibo = first_rp.model->ibo;
            size_t batch_index_count = first_rp.model->index_count;

            int ubo_slots[8];
            float colors[8 * 4];
            float glow_colors[8 * 4];
            int count = 0;
            int base_slot = -1;

            size_t next_idx = idx;
            while (next_idx < target_indices.size() && count < 8) {
                int p_idx = target_indices[next_idx];
                const auto& rp = state.render_palettes[p_idx];

                if (rp.model->vao != batch_vao) {
                    break;
                }

                int slot = state.player_ubo_slots[p_idx];
                if (base_slot < 0) {
                    base_slot = slot;
                }

                int current_min = base_slot;
                int current_max = base_slot;
                for (int k = 0; k < count; ++k) {
                    if (ubo_slots[k] < current_min) current_min = ubo_slots[k];
                    if (ubo_slots[k] > current_max) current_max = ubo_slots[k];
                }
                int next_min = std::min(current_min, slot);
                int next_max = std::max(current_max, slot);
                if (next_max - next_min >= 8) {
                    break;
                }

                ubo_slots[count] = slot;
                std::memcpy(&colors[count * 4], primary_color, sizeof(float) * 4);
                float resolved_outline_color[4];
                if (cfg.glow_enabled && cfg.outline_mode == "stencil") {
                    if (cfg.glow_health_based) {
                        float hp_factor = static_cast<float>(input.packet.players[p_idx].health) / 100.0f;
                        if (hp_factor < 0.0f) hp_factor = 0.0f;
                        if (hp_factor > 1.0f) hp_factor = 1.0f;
                        for (int c = 0; c < 4; ++c) {
                            resolved_outline_color[c] = cfg.glow_health_end[c] + hp_factor * (cfg.glow_health_start[c] - cfg.glow_health_end[c]);
                        }
                    } else {
                        std::memcpy(resolved_outline_color, cfg.glow_color, sizeof(float) * 4);
                    }
                } else if (secondary_color) {
                    std::memcpy(resolved_outline_color, secondary_color, sizeof(float) * 4);
                } else {
                    float default_glow[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                    std::memcpy(resolved_outline_color, default_glow, sizeof(float) * 4);
                }
                std::memcpy(&glow_colors[count * 4], resolved_outline_color, sizeof(float) * 4);

                count++;
                next_idx++;
            }

            if (count > 0) {
                float default_glow[4] = {1.0f, 0.0f, 0.0f, 1.0f};
                const float* pass_glow = secondary_color ? secondary_color : default_glow;
                bool is_stencil_outline = cfg.glow_enabled && cfg.outline_mode == "stencil";
                chams_renderer.render_mesh_instanced(
                    batch_vao, batch_ibo, batch_index_count,
                    style_id, pass_glow,
                    is_stencil_outline ? cfg.glow_thickness : 0.0f, current_glow_intensity, 0.0f,
                    cfg.flat_chams_no_overlap, ubo_slots, colors,
                    glow_colors, count,
                    is_stencil_outline
                );
            }
            idx = next_idx;
        }
    };

    if (has_prepass) {
        if ((style_invis_id > 0 || is_stencil_outline) && cfg.show_invisible) {
            glDepthFunc(GL_GREATER);
            glDepthMask(GL_FALSE);
            draw_player_batches(state.sorted_player_indices, style_invis_id, cfg.color_invis, cfg.color_invis_sec);
        }
        
        if (style_vis_id > 0 || is_stencil_outline) {
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
            draw_player_batches(state.sorted_player_indices, style_vis_id, cfg.color_vis, cfg.color_vis_sec);
        }
    } 
    else {
        static std::vector<int> visible_indices;
        static std::vector<int> invisible_indices;
        visible_indices.clear();
        invisible_indices.clear();
        visible_indices.reserve(state.sorted_player_indices.size());
        invisible_indices.reserve(state.sorted_player_indices.size());
        for (int i : state.sorted_player_indices) {
            if (state.render_palettes[i].is_visible) {
                visible_indices.push_back(i);
            } else {
                invisible_indices.push_back(i);
            }
        }

        if (style_vis_id > 0 || is_stencil_outline) {
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
            draw_player_batches(visible_indices, style_vis_id, cfg.color_vis, cfg.color_vis_sec);
        }

        if ((style_invis_id > 0 || is_stencil_outline) && cfg.show_invisible) {
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_TRUE);
            draw_player_batches(invisible_indices, style_invis_id, cfg.color_invis, cfg.color_invis_sec);
        }
    }

    chams_renderer.end_body_pass();
    glDepthMask(GL_TRUE); // Restore depth writes

    if (inputs.profiler) inputs.profiler->end_section(GpuTimerSection::BODY_PASS);

    // 4. Draw 3D Skeleton
    if (cfg.esp_enabled && cfg.esp_skeleton) {
        if (inputs.profiler) inputs.profiler->begin_section(GpuTimerSection::SKELETON_ESP);
        esp_renderer.clear();
        esp_renderer.set_projection(input.gl_vp);

        for (int i = 0; i < input.packet.player_count; ++i) {
            const auto& rp = state.render_palettes[i];
            if (!rp.model) continue;

            esp_renderer.add_skeleton_3d(rp.sanitized_bones, i, state.dummy_vis, cfg, false, 1.0f, nullptr);
        }

        if (has_prepass) {
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDepthRange(0.0, 0.998);

            // Pass 1: Hidden
            esp_renderer.upload_lines();
            
            glDepthFunc(GL_GREATER);
            esp_renderer.draw_lines_override(cfg.esp_skeleton_color_invis);

            // Pass 2: Visible
            glDepthFunc(GL_LEQUAL);
            esp_renderer.draw_lines_override(cfg.esp_skeleton_color_vis);

            glDepthRange(0.0, 1.0);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LEQUAL);
        } else {
            glDisable(GL_DEPTH_TEST);
            esp_renderer.flush_lines();
        }
        if (inputs.profiler) inputs.profiler->end_section(GpuTimerSection::SKELETON_ESP);
    }

}
