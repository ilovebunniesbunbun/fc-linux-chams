#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include "overlay/shm_reader.hpp" // For Vec3 and GrenadeType

struct GrenadeLineup {
    std::string title;
    std::string description;
    std::string throw_method;
    Vec3 position;
    Vec3 viewangles;
    uint8_t weapon;
};

class GrenadeHelperData {
public:
    GrenadeHelperData();
    ~GrenadeHelperData();

    void initialize(const std::string& resources_dir);
    void reload();

    // Gets lineups for a specific map. Thread-safe copy.
    std::vector<GrenadeLineup> get_lineups(const std::string& map_name) const;

private:
    void download_and_parse();
    void parse_file(const std::string& path, std::unordered_map<std::string, std::vector<GrenadeLineup>>& out_map);

    std::string resources_dir_;
    std::string web_json_path_;
    std::string local_json_path_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<GrenadeLineup>> map_lineups_;

    std::thread download_thread_;
    std::atomic<bool> is_downloading_{false};
};
