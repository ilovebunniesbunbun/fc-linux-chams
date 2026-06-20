#include "GrenadeTracker.hpp"
#include "logger.hpp"
#include "overlay/overlay_client.hpp"
#include <algorithm>

namespace {
    inline const char* get_grenade_name(uint8_t type) {
        switch (type) {
            case GRENADE_HE: return "HE";
            case GRENADE_FLASH: return "FLASH";
            case GRENADE_SMOKE: return "SMOKE";
            case GRENADE_MOLOTOV: return "FIRE";
            case GRENADE_DECOY: return "DECOY";
            default: return "GRENADE";
        }
    }
}

GrenadeTracker::GrenadeTracker() {}

void GrenadeTracker::update(const VischeckResult& render_result, double current_real_time, float dt, const OverlayConfig& cfg) {
    // Mark previously tracked active components as inactive
    for (auto& tracked : tracked_active_trajectories) {
        tracked.still_active = false;
    }
    for (auto& inf : tracked_infernos) {
        inf.still_active = false;
    }
    for (auto& warn : tracked_warnings) {
        warn.still_active = false;
    }

    // Match active trajectories and update lists
    for (const auto& active_traj : render_result.inflight_trajectories) {
        if (!active_traj.valid || active_traj.points.empty()) continue;

        // Filter out invalid entity handles (entry index 0xFFFF or 0x7FFF)
        uint32_t ent_idx = active_traj.entity_handle & 0xFFFF;
        if (active_traj.entity_handle == 0 || ent_idx == 0xFFFF || ent_idx == 0x7FFF) {
            continue;
        }

        // A. Persistent Warning Badge tracking
        bool warn_found = false;
        for (auto& warn : tracked_warnings) {
            if (warn.entity_handle == active_traj.entity_handle) {
                warn.position = active_traj.current_pos;
                if (warn.affected_count != active_traj.affected_count) {
                    if (cfg.debug_visibility) {
                        FC2_LOG_INFO("[DEBUG-VISIBILITY] Grenade Warning Badge Affected Count UPDATED. "
                                     "Handle: {} (0x{:08X}), Type: {} ({}), Old Count: {}, New Count: {}",
                                     warn.entity_handle, warn.entity_handle,
                                     warn.type, get_grenade_name(warn.type),
                                     warn.affected_count, active_traj.affected_count);
                    }
                }
                warn.affected_count = active_traj.affected_count;
                warn.still_active = true;
                warn.last_seen_time = current_real_time;
                warn_found = true;

                if (active_traj.timer_start_time > 0.0f && warn.timer_trigger_real_time == 0.0) {
                    warn.timer_trigger_real_time = current_real_time;
                    warn.timer_duration = active_traj.timer_duration;
                    if (cfg.debug_visibility) {
                        FC2_LOG_INFO("[DEBUG-VISIBILITY] Grenade Warning Timer STARTED/UPDATED. "
                                     "Reason: Server/Lua timer trigger received. "
                                     "Handle: {} (0x{:08X}), Type: {} ({}), Duration: {:.2f}s, Position: ({:.2f}, {:.2f}, {:.2f})",
                                     warn.entity_handle, warn.entity_handle,
                                     warn.type, get_grenade_name(warn.type),
                                     warn.timer_duration,
                                     warn.position.x, warn.position.y, warn.position.z);
                    }
                }
                break;
            }
        }
        if (!warn_found) {
            // Don't re-create warnings for handles whose timers have already expired
            bool is_completed = false;
            for (const auto& ch : completed_warning_handles) {
                if (ch.handle == active_traj.entity_handle) {
                    is_completed = true;
                    break;
                }
            }
            if (!is_completed) {
                TrackedWarning new_warn;
                new_warn.entity_handle = active_traj.entity_handle;
                new_warn.type = active_traj.type;
                new_warn.position = active_traj.current_pos;
                new_warn.first_seen_time = current_real_time;
                new_warn.last_seen_time = current_real_time;
                new_warn.timer_trigger_real_time = 0.0;
                if (active_traj.timer_start_time > 0.0f) {
                    new_warn.timer_trigger_real_time = current_real_time;
                }
                new_warn.timer_duration = active_traj.timer_duration;
                new_warn.affected_count = active_traj.affected_count;
                new_warn.still_active = true;
                tracked_warnings.push_back(new_warn);
                if (cfg.debug_visibility) {
                    FC2_LOG_INFO("[DEBUG-VISIBILITY] Grenade Warning Badge went VISIBLE. "
                                 "Reason: New projectile warning created. "
                                 "Handle: {} (0x{:08X}), Type: {} ({}), Position: ({:.2f}, {:.2f}, {:.2f}), "
                                 "Timer Duration: {:.2f}s, Timer Triggered: {}, Affected Enemies: {}",
                                 new_warn.entity_handle, new_warn.entity_handle,
                                 new_warn.type, get_grenade_name(new_warn.type),
                                 new_warn.position.x, new_warn.position.y, new_warn.position.z,
                                 new_warn.timer_duration,
                                 (new_warn.timer_trigger_real_time > 0.0) ? "YES" : "NO",
                                 new_warn.affected_count);
                }
            }
        }

        // B. Active Trajectory Path tracking
        // Check if already detonated / completed flight
        bool is_detonated = false;
        for (const auto& dh : detonated_handles) {
            if (dh.handle == active_traj.entity_handle) {
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

                // Check if flight duration has exceeded (marking the end of trajectory rendering)
                if (current_real_time - tracked.first_seen_time >= active_traj.duration) {
                    detonated_handles.push_back({tracked.entity_handle, current_real_time});
                    tracked.still_active = false; // Triggers moving to fading list
                }
                if (active_traj.timer_start_time > 0.0f && tracked.timer_trigger_real_time == 0.0) {
                    tracked.timer_trigger_real_time = current_real_time;
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
            new_track.timer_trigger_real_time = 0.0;
            if (active_traj.timer_start_time > 0.0f) {
                new_track.timer_trigger_real_time = current_real_time;
            }
            new_track.uploaded = false;

            tracked_active_trajectories.push_back(new_track);
            if (cfg.debug_visibility) {
                FC2_LOG_INFO("[DEBUG-VISIBILITY] In-Flight Grenade Trajectory went VISIBLE. "
                             "Reason: New projectile trajectory tracked. "
                             "Handle: {} (0x{:08X}), Type: {} ({}), Spawn Time: {:.3f}, Duration: {:.2f}s, "
                             "Start/Curr Pos: ({:.2f}, {:.2f}, {:.2f}), Points count: {}",
                             new_track.entity_handle, new_track.entity_handle,
                             active_traj.type, get_grenade_name(active_traj.type),
                             active_traj.spawn_time, active_traj.duration,
                             active_traj.current_pos.x, active_traj.current_pos.y, active_traj.current_pos.z,
                             active_traj.points.size());
            }
        }
    }

    // Match active infernos and update tracked infernos list
    for (int i = 0; i < render_result.inferno_count; ++i) {
        const auto& inf_data = render_result.infernos[i];
        if (!inf_data.active) continue;

        // Filter out invalid entity handles (entry index 0xFFFF or 0x7FFF)
        uint32_t ent_idx = inf_data.entity_handle & 0xFFFF;
        if (inf_data.entity_handle == 0 || ent_idx == 0xFFFF || ent_idx == 0x7FFF) {
            continue;
        }

        // If a ground fire inferno has spawned, kill any active or fading Molotov in-air warning badge near it
        if (inf_data.fire_count > 0) {
            Vec3 inf_center = inf_data.fire_positions[0];
            for (auto& w : tracked_warnings) {
                if (w.type == GRENADE_MOLOTOV) {
                    float dx = w.position.x - inf_center.x;
                    float dy = w.position.y - inf_center.y;
                    float dz = w.position.z - inf_center.z;
                    float dist_sqr = dx*dx + dy*dy + dz*dz;
                    if (dist_sqr < 300.0f * 300.0f) { // within 300 units
                        w.fade_alpha = 0.0f;
                        w.still_active = false;
                    }
                }
            }
        }

        bool found = false;
        for (auto& inf : tracked_infernos) {
            if (inf.entity_handle == inf_data.entity_handle) {
                inf.still_active = true;
                inf.last_seen_time = current_real_time;
                found = true;
                break;
            }
        }
        if (!found) {
            TrackedInferno new_inf;
            new_inf.entity_handle = inf_data.entity_handle;
            new_inf.first_seen_time = current_real_time;
            new_inf.duration = inf_data.duration;
            new_inf.still_active = true;
            new_inf.last_seen_time = current_real_time;
            tracked_infernos.push_back(new_inf);
            if (cfg.debug_visibility) {
                FC2_LOG_INFO("[DEBUG-VISIBILITY] Ground Fire Inferno went VISIBLE. "
                             "Reason: Ground fire (C_Inferno) spawned/detected. "
                             "Handle: {} (0x{:08X}), Duration: {:.2f}s, Flames count: {}",
                             new_inf.entity_handle, new_inf.entity_handle,
                             new_inf.duration, inf_data.fire_count);
            }
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

    // Clean up detonated handles after 30 seconds (must outlast smoke's 18s + decoy's 15s entity lifetime)
    detonated_handles.erase(
        std::remove_if(detonated_handles.begin(), detonated_handles.end(),
                       [current_real_time](const DetonatedHandle& dh) {
                           return (current_real_time - dh.detonated_time) > 30.0;
                       }),
        detonated_handles.end()
    );

    // Clean up completed warning handles after 30 seconds
    completed_warning_handles.erase(
        std::remove_if(completed_warning_handles.begin(), completed_warning_handles.end(),
                       [current_real_time](const CompletedWarningHandle& ch) {
                           return (current_real_time - ch.completed_time) > 30.0;
                       }),
        completed_warning_handles.end()
    );

    // Remove inactive trajectories from tracked active list
    tracked_active_trajectories.erase(
        std::remove_if(tracked_active_trajectories.begin(), tracked_active_trajectories.end(),
                       [](const TrackedTrajectory& t) { return !t.still_active; }),
        tracked_active_trajectories.end()
    );

    // Remove inactive tracked infernos after a 1.0 second grace period
    tracked_infernos.erase(
        std::remove_if(tracked_infernos.begin(), tracked_infernos.end(),
                       [current_real_time](const TrackedInferno& inf) { 
                           return !inf.still_active && (current_real_time - inf.last_seen_time) > 1.0; 
                       }),
        tracked_infernos.end()
    );

    // Collect completed warning handles before pruning, then prune
    {
        std::vector<TrackedWarning> surviving_warnings;
        surviving_warnings.reserve(tracked_warnings.size());

        for (auto w : tracked_warnings) {
            double age = current_real_time - w.first_seen_time;
            bool should_remove = false;

            // Update fade alpha and check if expired
            if (w.type == GRENADE_HE || w.type == GRENADE_FLASH || w.type == GRENADE_MOLOTOV) {
                float duration = (w.type == GRENADE_MOLOTOV) ? 2.02f : 1.5f;
                if (age >= duration) {
                    double overtime = age - duration;
                    w.fade_alpha = std::max(0.0f, 1.0f - static_cast<float>(overtime / 1.0));
                    if (w.fade_alpha <= 0.0f) {
                        should_remove = true;
                    }
                }
            } else if (w.type == GRENADE_SMOKE || w.type == GRENADE_DECOY) {
                if (w.timer_trigger_real_time > 0.0) {
                    double elapsed = current_real_time - w.timer_trigger_real_time;
                    if (elapsed >= w.timer_duration) {
                        double overtime = elapsed - w.timer_duration;
                        w.fade_alpha = std::max(0.0f, 1.0f - static_cast<float>(overtime / 1.0));
                        if (w.fade_alpha <= 0.0f) {
                            should_remove = true;
                        }
                    }
                }
            }

            // Also decay fade_alpha if entity stopped streaming early
            if (!should_remove && !w.still_active) {
                w.fade_alpha -= dt * 1.0f; // fade out over 1 second
                if (w.fade_alpha <= 0.0f) {
                    should_remove = true;
                }
            }

            // Safety cap based on grenade type (only active if not already fading)
            if (!should_remove && w.fade_alpha >= 1.0f) {
                if (w.type == GRENADE_HE || w.type == GRENADE_FLASH || w.type == GRENADE_MOLOTOV) {
                    should_remove = age > 5.0;
                } else if (w.type == GRENADE_DECOY) {
                    should_remove = age > 20.0;
                } else if (w.type == GRENADE_SMOKE) {
                    should_remove = age > 25.0;
                } else {
                    should_remove = age > 30.0;
                }
            }

            if (should_remove) {
                // Record as completed so it can't be re-created
                completed_warning_handles.push_back({w.entity_handle, current_real_time});
            } else {
                surviving_warnings.push_back(w);
            }
        }

        tracked_warnings = std::move(surviving_warnings);
    }

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



