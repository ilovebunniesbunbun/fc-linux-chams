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
    std::cout << "[*] Querying all models listing from VPK..." << std::endl;
    auto models = VmdlParser::ListAll("");
    std::cout << "[+] Found " << models.size() << " total models in VPK." << std::endl;

    // Print first 5 models as a sample
    int sample_count = std::min(static_cast<int>(models.size()), 5);
    for (int i = 0; i < sample_count; ++i) {
        std::cout << "  - Sample model: " << models[i].Path << std::endl;
    }

    // 3. Attempt loading a standard player model
    std::string target_model = "agents/models/ctm_sas/ctm_sas.vmdl";
    std::cout << "[*] Listing all files in VPK matching 'ctm_sas':" << std::endl;
    for (const auto& m : models) {
        if (m.Path.find("ctm_sas") != std::string::npos) {
            std::cout << "  - Found path: " << m.Path << std::endl;
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
    
    // Debug: read VPK directly and inspect KV3 tree
    {
        vpk::VPKDir dir;
        bool opened = false;
        for (const auto& p : vpk::cs2_default_vpk_paths()) {
            if (dir.open(p)) { opened = true; break; }
        }
        if (opened) {
            auto bytes = dir.read_file(target_model);
            if (bytes) {
                auto hdr_opt = source2::parse_res_header(bytes->data(), bytes->size());
                if (hdr_opt) {
                    const source2::ResBlock* ctrl_blk = source2::find_block(*hdr_opt, "CTRL");
                    if (ctrl_blk) {
                        auto ctrl_opt = source2::kv3::parse_binary(bytes->data() + ctrl_blk->offset, ctrl_blk->size);
                        if (ctrl_opt && ctrl_opt->is_object()) {
                            std::cout << "[DEBUG] Root keys in CTRL block: " << std::endl;
                            const auto& obj = ctrl_opt->as_object();
                            for (const auto& pair : obj) {
                                std::cout << "  - Key: " << pair.first << " (Type: " << (int)pair.second.type << ")" << std::endl;
                            }
                        }
                    }
                    const source2::ResBlock* data_blk = source2::find_block(*hdr_opt, "DATA");
                    if (data_blk) {
                        auto kv_opt = source2::kv3::parse_binary(bytes->data() + data_blk->offset, data_blk->size);
                        if (kv_opt && kv_opt->is_object()) {
                            std::cout << "[DEBUG] Root keys in DATA block: " << std::endl;
                            const auto& obj = kv_opt->as_object();
                            for (const auto& pair : obj) {
                                std::cout << "  - Key: " << pair.first << " (Type: " << (int)pair.second.type << ")" << std::endl;
                                if (pair.first == "m_refMeshes" || pair.first == "m_meshes" || pair.first == "m_modelInfo") {
                                    std::cout << "    - Value type: " << (int)pair.second.type << std::endl;
                                    if (pair.second.is_array()) {
                                        for (size_t i = 0; i < pair.second.size(); ++i) {
                                            const auto* val = pair.second.get(i);
                                            if (val) std::cout << "      - [" << i << "]: " << (val->is_string() ? val->as_string() : "non-string") << std::endl;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    auto mesh = VmdlParser::Load(target_model);

    if (!mesh.valid) {
        std::cerr << "[-] Failed to load model. Error: " << VmdlParser::LastError() << std::endl;
        return 1;
    }

    std::cout << "[+] Model loaded successfully!" << std::endl;
    std::cout << "  - Vertices count: " << mesh.vertices.size() << std::endl;
    std::cout << "  - Indices count:  " << mesh.indices.size() << std::endl;
    
    // Bounding Box Calculation
    if (!mesh.vertices.empty()) {
        float min_x = mesh.vertices[0].px, max_x = mesh.vertices[0].px;
        float min_y = mesh.vertices[0].py, max_y = mesh.vertices[0].py;
        float min_z = mesh.vertices[0].pz, max_z = mesh.vertices[0].pz;
        for (const auto& v : mesh.vertices) {
            if (v.px < min_x) min_x = v.px; if (v.px > max_x) max_x = v.px;
            if (v.py < min_y) min_y = v.py; if (v.py > max_y) max_y = v.py;
            if (v.pz < min_z) min_z = v.pz; if (v.pz > max_z) max_z = v.pz;
        }
        std::cout << "  - Bounding Box X: [" << min_x << ", " << max_x << "]" << std::endl;
        std::cout << "  - Bounding Box Y: [" << min_y << ", " << max_y << "]" << std::endl;
        std::cout << "  - Bounding Box Z: [" << min_z << ", " << max_z << "]" << std::endl;
    }

    std::cout << "  - Bone count:     " << mesh.bone_count << std::endl;
    std::cout << "  - Inv bind poses: " << mesh.inv_bind_poses.size() << std::endl;
    std::cout << "  - Bone parents:   " << mesh.bone_parents.size() << std::endl;

    return 0;
}
