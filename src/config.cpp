#include "config.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <type_traits>

#include "logger.hpp"

using json = nlohmann::json;

// JSON serialization implementations
void to_json(nlohmann::json& j, const Color& color)
{
    j = nlohmann::json::array({
        static_cast<int>(color.r * 255.0f),
        static_cast<int>(color.g * 255.0f),
        static_cast<int>(color.b * 255.0f),
        static_cast<int>(color.a * 255.0f)
    });
}

void from_json(const nlohmann::json& j, Color& color)
{
    if (j.is_array() && j.size() == 4) {
        color.r = j[0].get<float>() / 255.0f;
        color.g = j[1].get<float>() / 255.0f;
        color.b = j[2].get<float>() / 255.0f;
        color.a = j[3].get<float>() / 255.0f;
    }
}

void to_json(nlohmann::json& j, const ChamsStyle& style)
{
    j = (std::string)style;
}

void from_json(const nlohmann::json& j, ChamsStyle& style)
{
    style = ChamsStyle(j.get<std::string>());
}

OverlayConfig load_config(const std::string& filename)
{
    OverlayConfig cfg;
    std::string path = filename;
    std::ifstream file(path);
    if (!file.is_open()) {
        // Try parent directory
        path = "../" + filename;
        file.open(path);
    }
    if (!file.is_open()) {
        std::ofstream outfile(filename);
        if (outfile.is_open()) {
            json default_json;
#define X(type, name, json_key, default_val) default_json[json_key] = default_val;
            CONFIG_FIELDS
#undef X
            outfile << default_json.dump(4) << std::endl;
            outfile.close();
        }
        return cfg;
    }

    try {
        json j;
        file >> j;
        file.close();

#define X(type, name, json_key, default_val) \
        if (j.contains(json_key)) cfg.name = j[json_key].get<type>();
        CONFIG_FIELDS
#undef X
    } catch (...) {
    }

    if (cfg.scaling.empty()) cfg.scaling = "stretched";
    return cfg;
}

int get_style_id(const std::string& name)
{
    if (name == "textured") return 1;
    if (name == "flat") return 2;
    if (name == "metallic" || name == "fresnel") return 3;
    if (name == "glow_blend") return 5;
    if (name == "glow" || name == "cs2_glow") return 6;
    if (name == "wireframe") return 8;
    return 0;  // Disabled
}

void save_config(const std::string& filename, const OverlayConfig& cfg)
{
    json j;
#define X(type, name, json_key, default_val) j[json_key] = cfg.name;
    CONFIG_FIELDS
#undef X

    // Try saving in current directory first
    std::ofstream file(filename);
    if (!file.is_open()) {
        // Fall back to parent directory
        file.open("../" + filename);
    }
    if (file.is_open()) {
        file << j.dump(4) << std::endl;
        file.close();
        FC2_LOG_INFO("Saved config successfully.");
    } else {
        FC2_LOG_ERROR("Failed to save config to files.");
    }
}

std::vector<GpuDevice> detect_gpus()
{
    std::vector<GpuDevice> gpus;
    try {
        if (std::filesystem::exists("/sys/class/drm")) {
            for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm")) {
                std::string filename = entry.path().filename().string();
                if (filename.rfind("card", 0) == 0 && filename.find('-') == std::string::npos) {
                    std::string vendor_path = entry.path().string() + "/device/vendor";
                    std::string device_path = entry.path().string() + "/device/device";
                    std::string uevent_path = entry.path().string() + "/device/uevent";

                    std::string vendor = "";
                    std::string device = "";
                    std::string driver = "";

                    std::ifstream vf(vendor_path);
                    if (vf >> vendor) {
                        // strip any newline or spaces if read from stream
                    }
                    std::ifstream df(device_path);
                    if (df >> device) {
                        //
                    }

                    std::ifstream uf(uevent_path);
                    std::string line;
                    while (std::getline(uf, line)) {
                        if (line.rfind("DRIVER=", 0) == 0) {
                            driver = line.substr(7);
                        }
                    }

                    std::string display_name = "";
                    if (vendor == "0x10de" || vendor == "10de") {
                        display_name = "NVIDIA Dedicated GPU (" + filename + ")";
                    } else if (vendor == "0x1002" || vendor == "1002") {
                        display_name = "AMD Radeon GPU (" + filename + ")";
                    } else if (vendor == "0x8086" || vendor == "8086") {
                        display_name = "Intel Graphics GPU (" + filename + ")";
                    } else {
                        display_name = "Generic GPU (" + filename + ")";
                    }

                    gpus.push_back({filename, vendor, device, driver, display_name});
                }
            }
        }
    } catch (...) {
    }

    std::sort(gpus.begin(), gpus.end(), [](const GpuDevice& a, const GpuDevice& b) { return a.name < b.name; });
    return gpus;
}

bool validate_config(const std::string& filename, std::string& error_msg)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        file.open("../" + filename);
    }
    if (!file.is_open()) {
        error_msg = "Cannot open config file: " + filename;
        return false;
    }
    try {
        json j;
        file >> j;
        file.close();

#define X(type, name, json_key, default_val) \
        if (j.contains(json_key)) { \
            if (std::is_same<type, int>::value && !j[json_key].is_number_integer()) { \
                error_msg = "Field '" json_key "' must be an integer"; \
                return false; \
            } \
            if (std::is_same<type, bool>::value && !j[json_key].is_boolean()) { \
                error_msg = "Field '" json_key "' must be a boolean"; \
                return false; \
            } \
            if (std::is_same<type, float>::value && !j[json_key].is_number()) { \
                error_msg = "Field '" json_key "' must be a number"; \
                return false; \
            } \
            if (std::is_same<type, std::string>::value && !j[json_key].is_string()) { \
                error_msg = "Field '" json_key "' must be a string"; \
                return false; \
            } \
            if (std::is_same<type, Color>::value && (!j[json_key].is_array() || j[json_key].size() != 4)) { \
                error_msg = "Field '" json_key "' must be an array of 4 integers/floats"; \
                return false; \
            } \
            if (std::is_same<type, ChamsStyle>::value && !j[json_key].is_string()) { \
                error_msg = "Field '" json_key "' must be a string"; \
                return false; \
            } \
        }
        CONFIG_FIELDS
#undef X
        return true;
    } catch (const std::exception& e) {
        error_msg = e.what();
        return false;
    }
}
