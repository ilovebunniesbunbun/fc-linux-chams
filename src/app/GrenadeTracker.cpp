#include "GrenadeTracker.hpp"
#include <GL/glew.h>
#include <algorithm>

GrenadeTracker::GrenadeTracker() {}

void GrenadeTracker::update(const VischeckResult& render_result, double current_real_time, float dt, const OverlayConfig& cfg) {
    // Mark previously tracked trajectories as inactive
    for (auto& tracked : tracked_active_trajectories) {
        tracked.still_active = false;
    }

    // Match active trajectories and update tracked active list
    for (const auto& active_traj : render_result.inflight_trajectories) {
        if (!active_traj.valid || active_traj.points.empty()) continue;

        // Check if already detonated
        bool is_detonated = false;
        for (uint32_t h : detonated_handles) {
            if (h == active_traj.entity_handle) {
                is_detonated = true;
                break;
            }
        }
        if (is_detonated) continue;

        bool found = false;
        for (auto& tracked : tracked_active_trajectories) {
            if (tracked.entity_handle == active_traj.entity_handle) {
                tracked.traj = active_traj;
                tracked.still_active = true;
                found = true;

                // Check if flight duration has exceeded
                if (current_real_time - tracked.first_seen_time >= active_traj.duration) {
                    // Mark as detonated
                    detonated_handles.push_back(tracked.entity_handle);
                    tracked.still_active = false; // This triggers moving it to fading list
                }
                break;
            }
        }
        if (!found) {
            TrackedTrajectory new_track;
            new_track.entity_handle = active_traj.entity_handle;
            new_track.spawn_time = active_traj.spawn_time;
            new_track.traj = active_traj;
            new_track.still_active = true;
            new_track.first_seen_time = current_real_time;
            new_track.uploaded = false;

            tracked_active_trajectories.push_back(new_track);
        }
    }

    // Move newly inactive trajectories to the fading list
    for (const auto& tracked : tracked_active_trajectories) {
        if (!tracked.still_active) {
            FadingTrajectory fading;
            fading.traj = tracked.traj;
            fading.fade_alpha = 1.0f;
            fading.erase_progress = 0.0f;
            fading_trajectories.push_back(fading);
        }
    }

    // Clean up detonated handles that are no longer active in inflight_trajectories
    detonated_handles.erase(
        std::remove_if(detonated_handles.begin(), detonated_handles.end(),
                       [&render_result](uint32_t handle) {
                           for (const auto& active : render_result.inflight_trajectories) {
                               if (active.entity_handle == handle) return false; // Still active
                           }
                           return true; // No longer present, prune it!
                       }),
        detonated_handles.end()
    );

    // Remove inactive trajectories from tracked active list
    tracked_active_trajectories.erase(
        std::remove_if(tracked_active_trajectories.begin(), tracked_active_trajectories.end(),
                       [](const TrackedTrajectory& t) { return !t.still_active; }),
        tracked_active_trajectories.end()
    );

    // Update fading/erasure state of fading trajectories
    float fade_speed = 1.0f / (cfg.trajectory_fade_time > 0.05f ? cfg.trajectory_fade_time : 1.5f);
    for (auto& fading : fading_trajectories) {
        fading.fade_alpha -= dt * fade_speed;
        fading.erase_progress += dt * fade_speed;
    }

    // Remove fully faded/erased trajectories
    fading_trajectories.erase(
        std::remove_if(fading_trajectories.begin(), fading_trajectories.end(),
                       [](const FadingTrajectory& f) { return f.fade_alpha <= 0.0f || f.erase_progress >= 1.0f; }),
        fading_trajectories.end()
    );
}

void GrenadeTracker::draw(EspRenderer& esp_renderer, const VischeckResult& render_result, const float* gl_vp, const OverlayConfig& cfg) {
    if (!cfg.draw_grenade_trajectory) return;

    esp_renderer.clear();
    esp_renderer.set_projection(gl_vp);

    if (cfg.trajectory_show_through_walls) {
        glDisable(GL_DEPTH_TEST);
    } else {
        glEnable(GL_DEPTH_TEST);
    }
    glDepthMask(GL_FALSE);

    // B) Draw Held Grenade Trajectory (Commented out - currently disabled)
    /*
    if (render_result.held_trajectory.valid && !render_result.held_trajectory.points.empty()) {
        const auto& traj = render_result.held_trajectory;
        for (size_t i = 0; i < traj.points.size() - 1; ++i) {
            esp_renderer.add_line_3d(traj.points[i], traj.points[i + 1], cfg.grenade_trajectory_color, cfg.trajectory_thickness);
        }
        for (const auto& bounce : traj.bounces) {
            esp_renderer.add_box_instance(bounce, cfg.trajectory_bounce_size, cfg.trajectory_bounce_color);
        }
        esp_renderer.add_circle_instance(traj.end_pos, cfg.trajectory_detonation_radius, cfg.trajectory_detonation_color);
    }
    */

    // C) Draw In-Flight Grenade Trajectories
    if (!tracked_active_trajectories.empty()) {
        for (auto& tracked : tracked_active_trajectories) {
            if (!tracked.still_active) continue;
            const auto& traj = tracked.traj;
            if (!traj.valid || traj.points.empty()) continue;

            if (!tracked.uploaded) {
                esp_renderer.upload_trajectory(tracked.entity_handle, tracked.spawn_time, traj.points, cfg.grenade_trajectory_color);
                tracked.uploaded = true;
            }

            esp_renderer.draw_gpu_trajectory(tracked.entity_handle, 0.0f, 1.0f);

            // Draw bounce boxes
            for (const auto& bounce : traj.bounces) {
                esp_renderer.add_box_instance(bounce, cfg.trajectory_bounce_size, cfg.trajectory_bounce_color);
            }
            esp_renderer.add_circle_instance(traj.end_pos, cfg.trajectory_detonation_radius, cfg.trajectory_detonation_color);
        }
    }

    // D) Draw Fading/Erasing Grenade Trajectories
    if (!fading_trajectories.empty()) {
        for (const auto& fading : fading_trajectories) {
            const auto& traj = fading.traj;
            if (traj.points.empty()) continue;

            // Calculate point where erasure has reached
            float float_idx = static_cast<float>(traj.points.size() - 1) * fading.erase_progress;
            size_t start_idx = static_cast<size_t>(float_idx);
            if (start_idx >= traj.points.size() - 1) continue;

            // Modulate colors by fade_alpha
            float bounce_color_mod[4] = {
                cfg.trajectory_bounce_color[0],
                cfg.trajectory_bounce_color[1],
                cfg.trajectory_bounce_color[2],
                cfg.trajectory_bounce_color[3] * fading.fade_alpha
            };
            float detonation_color_mod[4] = {
                cfg.trajectory_detonation_color[0],
                cfg.trajectory_detonation_color[1],
                cfg.trajectory_detonation_color[2],
                cfg.trajectory_detonation_color[3] * fading.fade_alpha
            };

            esp_renderer.draw_gpu_trajectory(traj.entity_handle, fading.erase_progress, fading.fade_alpha);

            // Draw remaining bounce boxes
            for (size_t b_idx = 0; b_idx < traj.bounces.size(); ++b_idx) {
                const auto& bounce = traj.bounces[b_idx];
                size_t closest_pt_idx = (b_idx < traj.bounce_indices.size()) ? traj.bounce_indices[b_idx] : 0;
                if (closest_pt_idx >= start_idx) {
                    esp_renderer.add_box_instance(bounce, cfg.trajectory_bounce_size, bounce_color_mod);
                }
            }

            // Draw detonation circle if it hasn't been erased yet
            if (start_idx < traj.points.size() - 1) {
                esp_renderer.add_circle_instance(traj.end_pos, cfg.trajectory_detonation_radius, detonation_color_mod);
            }
        }
    }

    esp_renderer.flush_lines();
    esp_renderer.flush_instances();
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    // Prune GPU trajectory buffers that are no longer active or fading
    std::vector<uint32_t> active_handles;
    for (const auto& tracked : tracked_active_trajectories) {
        if (tracked.still_active) {
            active_handles.push_back(tracked.entity_handle);
        }
    }
    for (const auto& fading : fading_trajectories) {
        active_handles.push_back(fading.traj.entity_handle);
    }
    esp_renderer.prune_gpu_trajectories(active_handles);
}
