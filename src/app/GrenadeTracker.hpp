#pragma once
#include <vector>
#include <cstdint>
#include "overlay/trajectory_sim.hpp"
#include "config.hpp"
#include "VisibilityWorker.hpp"
#include "renderer/esp_renderer.hpp"

struct TrackedTrajectory {
    uint32_t entity_handle = 0;
    float spawn_time = 0.0f;
    TrajectoryResult traj;
    bool still_active = false;
    double first_seen_time = 0.0;
    bool uploaded = false;
};

struct FadingTrajectory {
    TrajectoryResult traj;
    float fade_alpha = 1.0f;
    float erase_progress = 0.0f;
};

class GrenadeTracker {
public:
    GrenadeTracker();

    // Update active, fading, and detonated states
    void update(const VischeckResult& render_result, double current_real_time, float dt, const OverlayConfig& cfg);

    // Draw all active, fading, and held trajectories
    void draw(EspRenderer& esp_renderer, const VischeckResult& render_result, const float* gl_vp, const OverlayConfig& cfg);

private:
    std::vector<TrackedTrajectory> tracked_active_trajectories;
    std::vector<FadingTrajectory> fading_trajectories;
    std::vector<uint32_t> detonated_handles;
};
