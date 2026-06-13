#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include "../shm_reader.hpp"
#include "bvh_parser.hpp"

struct TrajectoryResult {
    std::vector<Vec3> points;
    std::vector<Vec3> bounces;
    Vec3 end_pos{0,0,0};
    float duration = 0.0f;
    bool valid = false;

    // For real-time tracking and fading animations
    bool has_current_pos = false;
    Vec3 current_pos{0,0,0};
    uint32_t entity_handle = 0;
    float spawn_time = 0.0f;
};

// Check if grenade should detonate based on weapon type, velocity, and current tick
inline bool should_detonate(uint8_t weapon_type, const Vec3& vel, int tick) {
    constexpr float tick_interval = 1.0f / 64.0f;
    if (weapon_type == GRENADE_SMOKE || weapon_type == GRENADE_DECOY) {
        float speed_2d = std::sqrt(vel.x * vel.x + vel.y * vel.y);
        float threshold = (weapon_type == GRENADE_DECOY) ? 0.2f : 0.1f;
        int check_ticks = static_cast<int>(0.2f / tick_interval);
        if (check_ticks < 1) check_ticks = 1;
        return speed_2d < threshold && (tick % check_ticks) == 0;
    }
    else if (weapon_type == GRENADE_MOLOTOV) {
        return (tick * tick_interval) > 2.0f;
    }
    else if (weapon_type == GRENADE_FLASH || weapon_type == GRENADE_HE) {
        return ((tick - 8) * tick_interval) > 1.5f;
    }
    return false;
}

inline Vec3 resolve_collision(const Vec3& normal, const Vec3& vel) {
    constexpr float ELASTICITY = 0.45f;
    float backoff = (vel.x * normal.x + vel.y * normal.y + vel.z * normal.z) * 2.0f;

    Vec3 new_vel = {
        (vel.x - normal.x * backoff) * ELASTICITY,
        (vel.y - normal.y * backoff) * ELASTICITY,
        (vel.z - normal.z * backoff) * ELASTICITY
    };

    float speed_sqr = new_vel.x*new_vel.x + new_vel.y*new_vel.y + new_vel.z*new_vel.z;
    if (normal.z > 0.7f) {
        if (speed_sqr > 96000.0f) {
            float len = std::sqrt(speed_sqr);
            float nv_x = new_vel.x / len, nv_y = new_vel.y / len, nv_z = new_vel.z / len;
            float l = nv_x * normal.x + nv_y * normal.y + nv_z * normal.z;
            if (l > 0.5f) {
                float scale = 1.5f - l;
                new_vel.x *= scale; new_vel.y *= scale; new_vel.z *= scale;
            }
        }
        if (speed_sqr < 400.0f) {
            return {0, 0, 0};
        }
    }
    return new_vel;
}

inline void step_simulation(Vec3& pos, Vec3& vel, float gravity, const LocalMapBVH& bvh, bool& hit, Vec3& hit_normal) {
    constexpr float tick_interval = 1.0f / 64.0f;
    float new_vel_z = vel.z - gravity * tick_interval;

    Vec3 move = {
        vel.x * tick_interval,
        vel.y * tick_interval,
        (vel.z + new_vel_z) * 0.5f * tick_interval
    };

    Vec3 end_pos = { pos.x + move.x, pos.y + move.y, pos.z + move.z };
    Vec3 new_vel = { vel.x, vel.y, new_vel_z };

    hit = false;
    TraceResult result = bvh.trace_ray(pos, end_pos);
    if (result.hit) {
        hit = true;
        hit_normal = result.normal;
        // Slide slightly off normal to prevent getting stuck in walls
        end_pos = { result.end_pos.x + result.normal.x * 0.1f, result.end_pos.y + result.normal.y * 0.1f, result.end_pos.z + result.normal.z * 0.1f };
        new_vel = resolve_collision(hit_normal, new_vel);
    }

    pos = end_pos;
    vel = new_vel;
}

inline TrajectoryResult simulate_trajectory(const Vec3& origin, const Vec3& velocity, uint8_t weapon_type, const LocalMapBVH& bvh) {
    constexpr float tick_interval = 1.0f / 64.0f;
    constexpr float GRAVITY_SCALE = 0.4f;
    constexpr float sv_gravity = 800.0f;
    float gravity = sv_gravity * GRAVITY_SCALE;
    
    // Molotovs detonate instantly on terrain sloped < 55 degrees (cos(55) = 0.573)
    float molotov_max_slope_z = std::cos(55.0f * 3.14159265f / 180.0f);

    TrajectoryResult result;
    Vec3 pos = origin;
    Vec3 vel = velocity;
    int bounce_count = 0;
    int tick_timer = 0;
    
    constexpr int MAX_TICKS = 1024;
    constexpr int TICKS_PER_POINT = 4;

    for (int tick = 0; tick < MAX_TICKS; ++tick) {
        if (tick_timer == 0) {
            result.points.push_back(pos);
        }

        bool hit = false;
        Vec3 hit_normal{0,0,0};
        step_simulation(pos, vel, gravity, bvh, hit, hit_normal);

        if (hit) {
            bounce_count++;
            result.bounces.push_back(pos);

            bool is_molotov = (weapon_type == GRENADE_MOLOTOV);
            if (is_molotov && hit_normal.z >= molotov_max_slope_z) {
                result.end_pos = pos;
                result.duration = tick * tick_interval;
                result.valid = true;
                break;
            }
        }

        bool vel_stopped = std::abs(vel.x) < 20.0f && std::abs(vel.y) < 20.0f && (vel.x*vel.x + vel.y*vel.y + vel.z*vel.z) < 400.0f;

        if (should_detonate(weapon_type, vel, tick) || bounce_count > 20 || vel_stopped) {
            result.end_pos = pos;
            result.duration = tick * tick_interval;
            result.valid = true;
            break;
        }

        if (hit || tick_timer + 1 >= TICKS_PER_POINT) {
            tick_timer = 0;
        } else {
            tick_timer++;
        }
    }

    if (!result.points.empty() && result.valid) {
        const auto& last = result.points.back();
        float dx = last.x - result.end_pos.x;
        float dy = last.y - result.end_pos.y;
        float dz = last.z - result.end_pos.z;
        if ((dx*dx + dy*dy + dz*dz) > 1.0f) {
            result.points.push_back(result.end_pos);
        }
    }

    return result;
}

// Extract forward vector from View-Projection Matrix (Row-Major)
inline Vec3 extract_forward_vector(const float* vp) {
    // Row 2 contains depth information (viewing direction vector)
    float x = -vp[8];
    float y = -vp[9];
    float z = -vp[10];
    float len = std::sqrt(x*x + y*y + z*z);
    if (len > 1e-5f) {
        return { x / len, y / len, z / len };
    }
    return { 0.0f, 0.0f, -1.0f };
}
