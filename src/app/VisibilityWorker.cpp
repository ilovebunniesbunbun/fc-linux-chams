#include "VisibilityWorker.hpp"
#include "overlay/bvh_parser.hpp"
#include <algorithm>

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
                map_needs_reload = false;
                pending_map_mesh.Triangles.clear();
                cached_trajectories.clear();
            }
            
            local_packet = current_packet;
            local_cfg = cfg_input;
            has_work = false;
        }
        
        VischeckResult result;
        
        // 1. Local player held grenade trajectory simulation (Commented out - currently broken/lacks necessary bridge inputs)
        /*
        if (local_packet.held_grenade_type != GRENADE_NONE && local_packet.pin_pulled) {
            glm::vec3 forward = extract_forward_vector(local_packet.view_matrix);
            float clamped = std::max(15.0f, std::min(750.0f, 750.0f * 0.9f));
            float speed = (local_packet.throw_strength * 0.7f + 0.3f) * clamped;
            
            glm::vec3 origin = {
                local_packet.local_eye.x + forward.x * 16.0f,
                local_packet.local_eye.y + forward.y * 16.0f,
                local_packet.local_eye.z + forward.z * 16.0f + local_packet.throw_strength * 12.0f - 12.0f
            };
            
            glm::vec3 velocity = {
                forward.x * speed + local_packet.local_velocity.x * 1.25f,
                forward.y * speed + local_packet.local_velocity.y * 1.25f,
                forward.z * speed + local_packet.local_velocity.z * 1.25f
            };
            
            result.held_trajectory = simulate_trajectory(origin, velocity, local_packet.held_grenade_type, local_bvh);
        }
        */
        
        // 2. In-flight projectile trajectory simulation
        std::vector<TrajectoryResult> next_cache;
        int proj_count = std::min(local_packet.projectile_count, static_cast<int>(shm::MAX_PROJECTILES));
        for (int i = 0; i < proj_count; ++i) {
            const auto& proj = local_packet.projectiles[i];
            if (!proj.active) continue;
            
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
                traj = simulate_trajectory(proj.initial_position, proj.initial_velocity, proj.type, local_bvh);
                traj.has_current_pos = true;
                traj.entity_handle = proj.entity_handle;
                traj.spawn_time = proj.spawn_time;
            }

            traj.current_pos = proj.current_position;
            next_cache.push_back(traj);
            result.inflight_trajectories.push_back(traj);
        }
        cached_trajectories = std::move(next_cache);
        
        {
            std::lock_guard<std::mutex> lock(mutex);
            latest_result = std::move(result);
            has_new_result = true;
        }
    }
}
