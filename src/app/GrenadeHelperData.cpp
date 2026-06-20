#include "GrenadeHelperData.hpp"
#include "logger.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

namespace {
    uint8_t string_to_grenade_type(const std::string& str) {
        if (str == "he_grenade" || str == "hegrenade") return GRENADE_HE;
        if (str == "smoke_grenade" || str == "smokegrenade") return GRENADE_SMOKE;
        if (str == "flashbang") return GRENADE_FLASH;
        if (str == "molotov") return GRENADE_MOLOTOV;
        if (str == "decoy") return GRENADE_DECOY;
        return GRENADE_NONE;
    }

    std::string parse_throw_method(const json& movement) {
        if (!movement.is_object()) return "manual";
        std::vector<std::string> parts;
        if (movement.contains("throwtype") && movement["throwtype"].is_string()) {
            std::string tt = movement["throwtype"].get<std::string>();
            if (tt != "none") {
                parts.push_back(tt);
            }
        }
        if (movement.contains("modifiers") && movement["modifiers"].is_array()) {
            for (const auto& mod : movement["modifiers"]) {
                if (mod.is_string()) {
                    parts.push_back(mod.get<std::string>());
                }
            }
        }
        if (movement.contains("movement_key") && movement["movement_key"].is_string()) {
            std::string key = movement["movement_key"].get<std::string>();
            if (!key.empty()) {
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
                parts.push_back(key);
            }
        }
        if (movement.contains("jump") && movement["jump"].is_boolean()) {
            if (movement["jump"].get<bool>()) {
                parts.push_back("jump");
            }
        }
        if (parts.empty()) return "manual";
        std::string res;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) res += " + ";
            res += parts[i];
        }
        return res;
    }
}

GrenadeHelperData::GrenadeHelperData() = default;

GrenadeHelperData::~GrenadeHelperData() {
    if (download_thread_.joinable()) {
        download_thread_.join();
    }
}

void GrenadeHelperData::initialize(const std::string& resources_dir) {
    resources_dir_ = resources_dir;
    std::filesystem::create_directories(resources_dir_);
    web_json_path_ = resources_dir_ + "/cloud_grenades.json";
    local_json_path_ = resources_dir_ + "/local_grenades.json";

    if (!std::filesystem::exists(local_json_path_)) {
        std::ofstream outfile(local_json_path_);
        outfile << "{}" << std::endl;
        outfile.close();
    }

    reload();
}

void GrenadeHelperData::reload() {
    if (is_downloading_) return;
    is_downloading_ = true;

    if (download_thread_.joinable()) {
        download_thread_.join();
    }

    download_thread_ = std::thread([this]() {
        download_and_parse();
        is_downloading_ = false;
    });
}

void GrenadeHelperData::download_and_parse() {
    // Download via curl
    FC2_LOG_INFO("Downloading cloud grenades from bunnyware.fun...");
    std::string cmd = "curl -s -L https://bunnyware.fun/grenades.json -o \"" + web_json_path_ + "\"";
    int ret = std::system(cmd.c_str());
    if (ret == 0) {
        FC2_LOG_INFO("Successfully downloaded cloud grenades.");
    } else {
        FC2_LOG_ERROR("Failed to download cloud grenades. (curl exit code {})", ret);
    }

    std::unordered_map<std::string, std::vector<GrenadeLineup>> new_map;
    parse_file(web_json_path_, new_map);
    parse_file(local_json_path_, new_map);

    std::lock_guard<std::mutex> lock(mutex_);
    map_lineups_ = std::move(new_map);
}

void GrenadeHelperData::parse_file(const std::string& path, std::unordered_map<std::string, std::vector<GrenadeLineup>>& out_map) {
    if (!std::filesystem::exists(path)) return;

    std::ifstream file(path);
    if (!file.is_open()) return;

    json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        FC2_LOG_ERROR("Failed to parse JSON file {}: {}", path, e.what());
        return;
    }

    for (auto& [map_name, nade_list] : j.items()) {
        if (!nade_list.is_array()) continue;

        for (auto& nade_data : nade_list) {
            GrenadeLineup lineup;

            if (nade_data.contains("title") && nade_data["title"].is_string()) {
                lineup.title = nade_data["title"].get<std::string>();
            } else if (nade_data.contains("name") && nade_data["name"].is_array() && nade_data["name"].size() >= 2) {
                lineup.title = nade_data["name"][1].get<std::string>();
            }

            if (nade_data.contains("description") && nade_data["description"].is_string()) {
                lineup.description = nade_data["description"].get<std::string>();
            }

            if (nade_data.contains("movement")) {
                lineup.throw_method = parse_throw_method(nade_data["movement"]);
            } else {
                lineup.throw_method = "manual";
            }

            std::string weapon_str = "smoke_grenade";
            if (nade_data.contains("weapon") && nade_data["weapon"].is_string()) {
                weapon_str = nade_data["weapon"].get<std::string>();
            } else if (nade_data.contains("grenade_type") && nade_data["grenade_type"].is_string()) {
                weapon_str = nade_data["grenade_type"].get<std::string>();
            }
            lineup.weapon = string_to_grenade_type(weapon_str);

            if (nade_data.contains("position")) {
                auto& p = nade_data["position"];
                if (p.is_object() && p.contains("x")) {
                    lineup.position = Vec3(p["x"].get<float>(), p["y"].get<float>(), p["z"].get<float>());
                } else if (p.is_array() && p.size() >= 3) {
                    lineup.position = Vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
                }
            }

            auto get_angles = [](const json& a, Vec3& out) {
                if (a.is_object() && a.contains("x")) {
                    out = Vec3(a["x"].get<float>(), a["y"].get<float>(), 0.0f);
                    return true;
                } else if (a.is_array() && a.size() >= 2) {
                    out = Vec3(a[0].get<float>(), a[1].get<float>(), 0.0f);
                    return true;
                }
                return false;
            };

            if (nade_data.contains("view_angle")) {
                get_angles(nade_data["view_angle"], lineup.viewangles);
            } else if (nade_data.contains("angles")) {
                get_angles(nade_data["angles"], lineup.viewangles);
            } else if (nade_data.contains("viewangles")) {
                get_angles(nade_data["viewangles"], lineup.viewangles);
            }

            out_map[map_name].push_back(lineup);
        }
    }
}

std::vector<GrenadeLineup> GrenadeHelperData::get_lineups(const std::string& map_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = map_lineups_.find(map_name);
    if (it != map_lineups_.end()) {
        return it->second;
    }
    return {};
}
