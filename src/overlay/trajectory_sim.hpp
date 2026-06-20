#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include "overlay/shm_reader.hpp"
#include "overlay/bvh_parser.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

struct TrajectoryResult {
    std::vector<glm::vec3> points;
    std::vector<glm::vec3> bounces;
    std::vector<size_t> bounce_indices;
    glm::vec3 end_pos{0.0f, 0.0f, 0.0f};
    float duration = 0.0f;
    bool valid = false;

    // For real-time tracking and fading animations
    bool has_current_pos = false;
    glm::vec3 current_pos{0.0f, 0.0f, 0.0f};
    uint32_t entity_handle = 0;
    float spawn_time = 0.0f;
    float timer_start_time = 0.0f;
    float timer_duration = 0.0f;
    int affected_count = 0;
    uint8_t type = 0;
};

// Check if grenade should detonate based on weapon type, velocity, and current tick
inline bool should_detonate(uint8_t weapon_type, const glm::vec3& vel, int tick) {
    constexpr float tick_interval = 1.0f / 64.0f;
    if (weapon_type == GRENADE_SMOKE || weapon_type == GRENADE_DECOY) {
        float speed_2d = glm::length(glm::vec2(vel));
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

inline glm::vec3 resolve_collision(const glm::vec3& normal, const glm::vec3& vel) {
    constexpr float ELASTICITY = 0.45f;
    float backoff = glm::dot(vel, normal) * 2.0f;

    glm::vec3 new_vel = (vel - normal * backoff) * ELASTICITY;

    float speed_sqr = glm::dot(new_vel, new_vel);
    if (normal.z > 0.7f) {
        if (speed_sqr > 96000.0f) {
            float len = std::sqrt(speed_sqr);
            glm::vec3 nv = new_vel / len;
            float l = glm::dot(nv, normal);
            if (l > 0.5f) {
                float scale = 1.5f - l;
                new_vel *= scale;
            }
        }
        if (speed_sqr < 400.0f) {
            return glm::vec3(0.0f);
        }
    }
    return new_vel;
}

inline void step_simulation(glm::vec3& pos, glm::vec3& vel, float gravity, const LocalMapBVH& bvh, bool& hit, glm::vec3& hit_normal) {
    constexpr float tick_interval = 1.0f / 64.0f;
    float new_vel_z = vel.z - gravity * tick_interval;

    glm::vec3 move = glm::vec3(glm::vec2(vel), (vel.z + new_vel_z) * 0.5f) * tick_interval;

    glm::vec3 end_pos = pos + move;
    glm::vec3 new_vel = glm::vec3(glm::vec2(vel), new_vel_z);

    hit = false;
    TraceResult result = bvh.trace_ray(pos, end_pos);
    if (result.hit) {
        hit = true;
        hit_normal = result.normal;
        // Slide slightly off normal to prevent getting stuck in walls
        end_pos = result.end_pos + result.normal * 0.1f;
        new_vel = resolve_collision(hit_normal, new_vel);
    }

    pos = end_pos;
    vel = new_vel;
}

inline TrajectoryResult simulate_trajectory(const glm::vec3& origin, const glm::vec3& velocity, uint8_t weapon_type, const LocalMapBVH& bvh) {
    constexpr float tick_interval = 1.0f / 64.0f;
    constexpr float GRAVITY_SCALE = 0.4f;
    constexpr float sv_gravity = 800.0f;
    float gravity = sv_gravity * GRAVITY_SCALE;
    
    // Molotovs detonate instantly on terrain sloped < 55 degrees (cos(55) = 0.573)
    float molotov_max_slope_z = std::cos(55.0f * 3.14159265f / 180.0f);

    TrajectoryResult result;
    glm::vec3 pos = origin;
    glm::vec3 vel = velocity;
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

        bool vel_stopped = std::abs(vel.x) < 20.0f && std::abs(vel.y) < 20.0f && glm::dot(vel, vel) < 400.0f;

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
        if (glm::distance2(last, result.end_pos) > 1.0f) {
            result.points.push_back(result.end_pos);
        }
    }

    for (const auto& bounce : result.bounces) {
        size_t closest_pt_idx = 0;
        float min_d = 1e9f;
        for (size_t idx = 0; idx < result.points.size(); ++idx) {
            float d = glm::distance2(result.points[idx], bounce);
            if (d < min_d) {
                min_d = d;
                closest_pt_idx = idx;
            }
        }
        result.bounce_indices.push_back(closest_pt_idx);
    }

    return result;
}

// Extract forward vector from View-Projection Matrix (Row-Major)
inline glm::vec3 extract_forward_vector(const float* vp) {
    // Row 2 contains depth information (viewing direction vector)
    glm::vec3 dir(-vp[8], -vp[9], -vp[10]);
    float len = glm::length(dir);
    if (len > 1e-5f) {
        return dir / len;
    }
    return glm::vec3(0.0f, 0.0f, -1.0f);
}
