#include "VisibilityWorker.hpp"
#include "overlay/bvh_parser.hpp"
#include <algorithm>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/norm.hpp>

VisibilityWorker::VisibilityWorker() {}

VisibilityWorker::~VisibilityWorker() {
    stop();
}

void VisibilityWorker::start() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!worker_thread.joinable()) {
        quit = false;
        has_work = false;
        worker_thread = std::thread(&VisibilityWorker::run, this);
    }
}

void VisibilityWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (worker_thread.joinable()) {
            quit = true;
            has_work = true;
            cv.notify_one();
        } else {
            return;
        }
    }
    worker_thread.join();
}

void VisibilityWorker::submit_work(const ShmPacket& packet, const OverlayConfig& cfg) {
    std::lock_guard<std::mutex> lock(mutex);
    current_packet = packet;
    cfg_input = cfg;
    has_work = true;
    cv.notify_one();
}

void VisibilityWorker::update_map(const MapParser::MapMesh& mesh, const OverlayConfig& cfg) {
    std::lock_guard<std::mutex> lock(mutex);
    pending_map_mesh = mesh;
    map_needs_reload = true;
    cfg_input = cfg;
    has_work = true;
    cv.notify_one();
}

bool VisibilityWorker::get_latest_result(VischeckResult& out_result) {
    std::lock_guard<std::mutex> lock(mutex);
    if (has_new_result) {
        out_result = std::move(latest_result);
        has_new_result = false;
        return true;
    }
    return false;
}

void VisibilityWorker::run() {
    LocalMapBVH local_bvh;
    ShmPacket local_packet;
    OverlayConfig local_cfg;
    std::vector<TrajectoryResult> cached_trajectories;
    
    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this]() { return quit || has_work; });
            
            if (quit) {
                break;
            }
            
            if (map_needs_reload) {
                local_bvh.triangles.clear();
                for (const auto& tri : pending_map_mesh.Triangles) {
                    LocalMapBVH::Triangle t;
                    t.v0 = { tri.v0.pos.x, tri.v0.pos.y, tri.v0.pos.z };
                    t.v1 = { tri.v1.pos.x, tri.v1.pos.y, tri.v1.pos.z };
                    t.v2 = { tri.v2.pos.x, tri.v2.pos.y, tri.v2.pos.z };
                    local_bvh.triangles.push_back(t);
                }
                local_bvh.build();
                
                // Cache the map doors
                local_doors = pending_map_mesh.Doors;
                
                map_needs_reload = false;
                pending_map_mesh.Triangles.clear();
                pending_map_mesh.Doors.clear();
                cached_trajectories.clear();
            }
            
            local_packet = current_packet;
            local_cfg = cfg_input;
            has_work = false;
        }
        
        VischeckResult result;
        
        // Transform active doors to world space
        std::vector<LocalMapBVH::Triangle> active_door_triangles;
        int active_doors_count = std::min(local_packet.door_count, static_cast<int>(shm::MAX_DOORS));
        for (int i = 0; i < active_doors_count; ++i) {
            const auto& live_door = local_packet.doors[i];
            if (!live_door.active) continue;

            // Find closest MapDoor in local_doors (mutable pointer)
            MapParser::MapDoor* best_match = nullptr;
            float best_dist_sq = 2500.0f; // 50 units threshold squared
            for (auto& md : local_doors) {
                glm::vec3 diff = { md.StaticOrigin.x - live_door.origin.x,
                                   md.StaticOrigin.y - live_door.origin.y,
                                   md.StaticOrigin.z - live_door.origin.z };
                float dist_sq = glm::dot(diff, diff);
                if (dist_sq < best_dist_sq) {
                    best_dist_sq = dist_sq;
                    best_match = &md;
                }
            }

            if (best_match) {
                // Dynamically reload the model if the bridge reports a different model (e.g. damaged)
                std::string norm_live_model = live_door.model_name;
                for (char& c : norm_live_model) {
                    if (c == '\\') c = '/';
                    else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                }
                if (norm_live_model.size() >= 5 && norm_live_model.compare(norm_live_model.size() - 5, 5, ".vmdl") == 0) {
                    norm_live_model += "_c";
                }

                if (!norm_live_model.empty() && best_match->ModelName != norm_live_model) {
                    auto new_tris = MapParser::LoadModelTriangles(live_door.model_name);
                    if (!new_tris.empty()) {
                        best_match->Triangles.clear();
                        best_match->ModelName = norm_live_model;
                        for (const auto& tri : new_tris) {
                            MapParser::Triangle scaled_tri = tri;
                            scaled_tri.v0.pos.x *= best_match->Scales.x;
                            scaled_tri.v0.pos.y *= best_match->Scales.y;
                            scaled_tri.v0.pos.z *= best_match->Scales.z;
                            scaled_tri.v1.pos.x *= best_match->Scales.x;
                            scaled_tri.v1.pos.y *= best_match->Scales.y;
                            scaled_tri.v1.pos.z *= best_match->Scales.z;
                            scaled_tri.v2.pos.x *= best_match->Scales.x;
                            scaled_tri.v2.pos.y *= best_match->Scales.y;
                            scaled_tri.v2.pos.z *= best_match->Scales.z;
                            best_match->Triangles.push_back(scaled_tri);
                        }
                    }
                }

                // Compute rotation matrix for the live door's angles (pitch, yaw, roll)
                float pitch_rad = live_door.angles.x * (3.14159265f / 180.f);
                float yaw_rad   = live_door.angles.y * (3.14159265f / 180.f);
                float roll_rad  = live_door.angles.z * (3.14159265f / 180.f);

                float cx = std::cos(roll_rad); float sx = std::sin(roll_rad);
                float cy = std::cos(pitch_rad); float sy = std::sin(pitch_rad);
                float cz = std::cos(yaw_rad); float sz = std::sin(yaw_rad);

                float r00 = cz * cy; float r01 = cz * sx * sy - sz * cx; float r02 = cz * cx * sy + sz * sx;
                float r10 = sz * cy; float r11 = sz * sx * sy + cz * cx; float r12 = sz * cx * sy - cz * sx;
                float r20 = -sy; float r21 = sx * cy; float r22 = cx * cy;

                auto TransformPoint = [&](const glm::vec3& p) -> glm::vec3 {
                    float rx = r00 * p.x + r01 * p.y + r02 * p.z;
                    float ry = r10 * p.x + r11 * p.y + r12 * p.z;
                    float rz = r20 * p.x + r21 * p.y + r22 * p.z;
                    return { rx + live_door.origin.x, ry + live_door.origin.y, rz + live_door.origin.z };
                };

                for (const auto& tri : best_match->Triangles) {
                    LocalMapBVH::Triangle trans_tri;
                    trans_tri.v0 = TransformPoint({ tri.v0.pos.x, tri.v0.pos.y, tri.v0.pos.z });
                    trans_tri.v1 = TransformPoint({ tri.v1.pos.x, tri.v1.pos.y, tri.v1.pos.z });
                    trans_tri.v2 = TransformPoint({ tri.v2.pos.x, tri.v2.pos.y, tri.v2.pos.z });
                    active_door_triangles.push_back(trans_tri);
                }
            }
        }
        result.active_door_triangles = active_door_triangles;
        
        // Helper to check for enemy impact scans using map BVH occlusion
        auto count_affected_enemies = [&](const glm::vec3& detonation_pos, uint8_t weapon_type) -> int {
            float radius_sqr = 0.0f;
            bool is_flash = false;
            
            if (weapon_type == GRENADE_HE) radius_sqr = 350.0f * 350.0f;
            else if (weapon_type == GRENADE_MOLOTOV) radius_sqr = 150.0f * 150.0f;
            else if (weapon_type == GRENADE_FLASH) {
                radius_sqr = 1000.0f * 1000.0f;
                is_flash = true;
            } else {
                return 0;
            }
            
            int affected_count = 0;
            for (int i = 0; i < local_packet.player_count; ++i) {
                const auto& player = local_packet.players[i];
                if (!player.active || player.health <= 0) continue;
                
                // Read local player's team to ensure we only count enemies
                // (Lua code checks local team. Our packet contains the enemy players list already,
                // but let's double check distance and LoS)
                glm::vec3 head_pos = player.bones[7].position; // bone 7 is head/eye
                glm::vec3 delta = head_pos - detonation_pos;
                float dist_sq = glm::dot(delta, delta);
                if (dist_sq <= radius_sqr) {
                    // Trace occlusion ray from detonation center to player head (including dynamic doors)
                    TraceResult tr = local_bvh.trace_ray(detonation_pos, head_pos, active_door_triangles);
                    if (!tr.hit || glm::distance2(tr.end_pos, head_pos) < 100.0f) {
                        if (is_flash) {
                            // Derive player's view vector from head bone rotation quaternion
                            // (Rotation rotation is a quaternion vec4_t: x,y,z,w)
                            glm::quat q(player.bones[7].rotation.w, player.bones[7].rotation.x, player.bones[7].rotation.y, player.bones[7].rotation.z);
                            glm::vec3 forward = q * glm::vec3(0.0f, 0.0f, -1.0f); // Default forward vector
                            glm::vec3 to_nade = glm::normalize(detonation_pos - head_pos);
                            if (glm::dot(forward, to_nade) > -0.5f) { // ~120 degrees FOV check
                                affected_count++;
                            }
                        } else {
                            affected_count++;
                        }
                    }
                }
            }
            return affected_count;
        };

        // 1. Local player held grenade trajectory simulation
        if (local_packet.held_grenade_type != GRENADE_NONE && local_packet.pin_pulled) {
            float pitch = local_packet.local_angles.x;
            float yaw = local_packet.local_angles.y;

            // Normalize pitch to [-90, 90]
            if (pitch > 90.0f) pitch -= 360.0f;
            else if (pitch < -90.0f) pitch += 360.0f;

            // Apply CS2 pitch throw offset correction
            pitch = pitch - (90.0f - std::abs(pitch)) * 10.0f / 90.0f;

            float p_rad = pitch * 3.14159265f / 180.0f;
            float y_rad = yaw * 3.14159265f / 180.0f;
            
            glm::vec3 forward(
                std::cos(p_rad) * std::cos(y_rad),
                std::cos(p_rad) * std::sin(y_rad),
                -std::sin(p_rad)
            );

            float strength = local_packet.throw_strength;
            if (std::abs(strength - 0.5f) <= 0.1f) {
                strength = 0.5f;
            }

            float clamped = std::max(15.0f, std::min(750.0f, 750.0f * 0.9f));
            float speed = (strength * 0.7f + 0.3f) * clamped;
            
            glm::vec3 origin = {
                local_packet.local_eye.x + forward.x * 16.0f,
                local_packet.local_eye.y + forward.y * 16.0f,
                local_packet.local_eye.z + forward.z * 16.0f + strength * 12.0f - 12.0f
            };

            // Raytrace checks to prevent throw origin clipping into walls (including dynamic doors)
            glm::vec3 trace_end = origin + forward * 22.0f;
            TraceResult tr = local_bvh.trace_ray(origin, trace_end, active_door_triangles);
            if (tr.hit) {
                origin = tr.end_pos - forward * 6.0f;
            } else {
                origin = origin + forward * 16.0f;
            }
            
            glm::vec3 velocity = {
                forward.x * speed + local_packet.local_velocity.x * 1.25f,
                forward.y * speed + local_packet.local_velocity.y * 1.25f,
                forward.z * speed + local_packet.local_velocity.z * 1.25f
            };
            
            result.held_trajectory = simulate_trajectory(origin, velocity, local_packet.held_grenade_type, local_bvh, active_door_triangles);
            result.held_trajectory.type = local_packet.held_grenade_type;
            if (result.held_trajectory.valid && result.held_trajectory.points.size() >= 2) {
                result.held_trajectory.affected_count = count_affected_enemies(result.held_trajectory.end_pos, local_packet.held_grenade_type);
            }
        }
        
        // 2. In-flight projectile trajectory simulation
        std::vector<TrajectoryResult> next_cache;
        int proj_count = std::min(local_packet.projectile_count, static_cast<int>(shm::MAX_PROJECTILES));
        for (int i = 0; i < proj_count; ++i) {
            const auto& proj = local_packet.projectiles[i];
            if (!proj.active || proj.entity_handle < 0x1000) continue;
            
            bool found_cached = false;
            TrajectoryResult traj;
            for (const auto& cached_traj : cached_trajectories) {
                if (cached_traj.entity_handle == proj.entity_handle) {
                    traj = cached_traj;
                    found_cached = true;
                    break;
                }
            }

            if (!found_cached) {
                traj = simulate_trajectory(proj.initial_position, proj.initial_velocity, proj.type, local_bvh, active_door_triangles);
                traj.has_current_pos = true;
                traj.entity_handle = proj.entity_handle;
                traj.spawn_time = proj.spawn_time;
            }

            traj.type = proj.type;
            traj.current_pos = proj.current_position;
            traj.timer_start_time = proj.timer_start_time;
            traj.timer_duration = proj.duration;

            // Re-calculate affected enemies at predicted detonation point
            if (traj.valid && traj.points.size() >= 2) {
                traj.affected_count = count_affected_enemies(traj.end_pos, proj.type);
            }

            next_cache.push_back(traj);
            result.inflight_trajectories.push_back(traj);
        }
        cached_trajectories = std::move(next_cache);
        
        // 3. Ground Inferno Zones copy
        result.inferno_count = std::min(local_packet.inferno_count, 4);
        for (int i = 0; i < result.inferno_count; ++i) {
            result.infernos[i] = local_packet.infernos[i];
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            latest_result = std::move(result);
            has_new_result = true;
        }
    }
}

