#include <iostream>
#include <vector>
#include "vpk/vmdl/vmdl.hpp"
#include "vpk/vpk.hpp"

int main() {
    std::cout << "--- FC2 CHAMS V2: Standing Model Load Test ---" << std::endl;

    // 1. Scan default VPK paths
    std::cout << "[*] Scanning default VPK paths on Linux..." << std::endl;
    auto paths = vpk::cs2_default_vpk_paths();
    if (paths.empty()) {
        std::cerr << "[-] No default CS2 VPK paths discovered." << std::endl;
    } else {
        for (const auto& p : paths) {
            std::cout << "  - Found: " << p << std::endl;
        }
    }

    // 2. Query VPK files
    std::cout << "[*] Querying models listing from VPK..." << std::endl;
    auto models = VmdlParser::ListAll("characters/models");
    std::cout << "[+] Found " << models.size() << " models under 'characters/models'." << std::endl;

    if (models.empty()) {
        std::cout << "[-] Listing is empty, listing all models in archive..." << std::endl;
        models = VmdlParser::ListAll("");
        std::cout << "[+] Total models in archive: " << models.size() << std::endl;
    }

    // Print first 5 models as a sample
    int sample_count = std::min(static_cast<int>(models.size()), 5);
    for (int i = 0; i < sample_count; ++i) {
        std::cout << "  - Sample model: " << models[i].Path << std::endl;
    }

    // 3. Attempt loading a standard player model
    std::string target_model = "";
    for (const auto& m : models) {
        if (m.Path.find("ctm_sas") != std::string::npos || m.Path.find("tm_balkan") != std::string::npos) {
            target_model = m.Path;
            break;
        }
    }

    if (target_model.empty() && !models.empty()) {
        target_model = models[0].Path;
    }

    if (target_model.empty()) {
        std::cerr << "[-] No models found to load." << std::endl;
        return 1;
    }

    std::cout << "[*] Loading model: " << target_model << "..." << std::endl;
    auto mesh = VmdlParser::Load(target_model);

    if (!mesh.valid) {
        std::cerr << "[-] Failed to load model. Error: " << VmdlParser::LastError() << std::endl;
        return 1;
    }

    std::cout << "[+] Model loaded successfully!" << std::endl;
    std::cout << "  - Vertices count: " << mesh.vertices.size() << std::endl;
    std::cout << "  - Indices count:  " << mesh.indices.size() << std::endl;
    std::cout << "  - Bone count:     " << mesh.bone_count << std::endl;
    std::cout << "  - Inv bind poses: " << mesh.inv_bind_poses.size() << std::endl;
    std::cout << "  - Bone parents:   " << mesh.bone_parents.size() << std::endl;

    return 0;
}
