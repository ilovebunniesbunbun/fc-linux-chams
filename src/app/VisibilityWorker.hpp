#pragma once
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include "overlay/shm_reader.hpp"
#include "config.hpp"
#include "overlay/trajectory_sim.hpp"
#include "vpk/vmdl/maps/map_parser.hpp"

struct VischeckResult {
    TrajectoryResult held_trajectory;
    std::vector<TrajectoryResult> inflight_trajectories;
    int inferno_count = 0;
    InfernoData infernos[4];
    std::vector<LocalMapBVH::Triangle> active_door_triangles;
};

class VisibilityWorker {
public:
    VisibilityWorker();
    ~VisibilityWorker();

    void start();
    void stop();

    // Submit new packet and config to process
    void submit_work(const ShmPacket& packet, const OverlayConfig& cfg);

    // Signal map geometry reload
    void update_map(const MapParser::MapMesh& mesh, const OverlayConfig& cfg);

    // Retrieve latest results if available. Returns true if there was a new result.
    bool get_latest_result(VischeckResult& out_result);

private:
    void run();

    std::thread worker_thread;
    std::mutex mutex;
    std::condition_variable cv;
    bool quit = false;
    bool has_work = false;

    // Inputs
    ShmPacket current_packet;
    MapParser::MapMesh pending_map_mesh;
    bool map_needs_reload = false;
    OverlayConfig cfg_input;
    std::vector<MapParser::MapDoor> local_doors;

    // Outputs
    VischeckResult latest_result;
    bool has_new_result = false;
};
