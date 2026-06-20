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
    double timer_trigger_real_time = 0.0;
    bool uploaded = false;
};

struct FadingTrajectory {
    TrajectoryResult traj;
    float fade_alpha = 1.0f;
    float erase_progress = 0.0f;
};

class OverlayClient;

class GrenadeTracker {
public:
    GrenadeTracker();

    // Update active, fading, and detonated states
    void update(const VischeckResult& render_result, double current_real_time, float dt, const OverlayConfig& cfg);

    struct TrackedInferno {
        uint32_t entity_handle;
        double first_seen_time;
        float duration;
        bool still_active;
        double last_seen_time;
    };

    struct TrackedWarning {
        uint32_t entity_handle;
        uint8_t type;
        Vec3 position;
        double first_seen_time;
        double last_seen_time;
        double timer_trigger_real_time;
        float timer_duration;
        int affected_count;
        bool still_active;
        float fade_alpha = 1.0f;
    };

    std::vector<TrackedTrajectory>& get_tracked_active_trajectories() { return tracked_active_trajectories; }
    const std::vector<TrackedTrajectory>& get_tracked_active_trajectories() const { return tracked_active_trajectories; }

    std::vector<FadingTrajectory>& get_fading_trajectories() { return fading_trajectories; }
    const std::vector<FadingTrajectory>& get_fading_trajectories() const { return fading_trajectories; }

    const std::vector<TrackedInferno>& get_tracked_infernos() const { return tracked_infernos; }
    const std::vector<TrackedWarning>& get_tracked_warnings() const { return tracked_warnings; }

private:
    std::vector<TrackedTrajectory> tracked_active_trajectories;
    std::vector<FadingTrajectory> fading_trajectories;
    struct DetonatedHandle {
        uint32_t handle;
        double detonated_time;
    };
    std::vector<DetonatedHandle> detonated_handles;

    // Prevents re-creation of warning badges after their timer has fully expired
    struct CompletedWarningHandle {
        uint32_t handle;
        double completed_time;
    };
    std::vector<CompletedWarningHandle> completed_warning_handles;
    std::vector<TrackedInferno> tracked_infernos;
    std::vector<TrackedWarning> tracked_warnings;
};
