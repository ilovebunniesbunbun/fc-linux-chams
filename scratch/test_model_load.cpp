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
    
    // Debug: read VPK directly and inspect ModelData
    {
        vpk::VPKDir dir;
        bool opened = false;
        for (const auto& p : vpk::cs2_default_vpk_paths()) {
            if (dir.open(p)) { opened = true; break; }
        }
        if (opened) {
            std::string read_path = target_model;
            if (read_path.find(".vmdl_c") == std::string::npos && read_path.find(".vmdl") != std::string::npos) {
                read_path += "_c";
            }
            auto bytes = dir.read_file(read_path);
            if (bytes) {
                auto is_kv_int = [](source2::kv3::KVType t) {
                    return t == source2::kv3::KVType::Int64 || t == source2::kv3::KVType::UInt64 ||
                           t == source2::kv3::KVType::Int32 || t == source2::kv3::KVType::UInt32 ||
                           t == source2::kv3::KVType::Int16 || t == source2::kv3::KVType::UInt16 ||
                           t == source2::kv3::KVType::Int64_zero || t == source2::kv3::KVType::Int64_one ||
                           t == source2::kv3::KVType::Int32_as_byte;
                };

                auto hdr_opt = source2::parse_res_header(bytes->data(), bytes->size());
                if (hdr_opt) {
                    const source2::ResBlock* ctrl_blk = source2::find_block(*hdr_opt, "CTRL");
                    if (ctrl_blk) {
                        auto ctrl_opt = source2::kv3::parse_binary(bytes->data() + ctrl_blk->offset, ctrl_blk->size);
                        if (ctrl_opt && ctrl_opt->is_object()) {
                            std::cout << "[DEBUG] embedded_meshes in CTRL:" << std::endl;
                            const auto* embedded = ctrl_opt->get("embedded_meshes");
                            if (embedded && embedded->is_array()) {
                                std::cout << "  embedded_meshes size: " << embedded->size() << std::endl;
                                for (size_t ei = 0; ei < embedded->size(); ++ei) {
                                    const auto* em = embedded->get(ei);
                                    if (em && em->is_object()) {
                                        std::cout << "  Mesh [" << ei << "]:" << std::endl;
                                        for (const auto& pair : em->as_object()) {
                                            std::cout << "    - " << pair.first << " (Type: " << (int)pair.second.type << ")";
                                            if (pair.second.is_string()) {
                                                std::cout << " = \"" << pair.second.as_string() << "\"";
                                            } else if (is_kv_int(pair.second.type)) {
                                                std::cout << " = " << pair.second.as_int();
                                            }
                                            std::cout << std::endl;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    const source2::ResBlock* data_blk = source2::find_block(*hdr_opt, "DATA");
                    if (data_blk) {
                        auto data_opt = source2::kv3::parse_binary(bytes->data() + data_blk->offset, data_blk->size);
                        if (data_opt && data_opt->is_object()) {
                            std::cout << "[DEBUG] DATA block keys:" << std::endl;
                            for (const auto& pair : data_opt->as_object()) {
                                std::cout << "  Key: " << pair.first << " (Type: " << (int)pair.second.type << ")";
                                if (pair.first == "m_meshGroups" || pair.first == "m_refMeshGroupMasks" || pair.first == "m_refMeshes" || pair.first == "m_modelInfo") {
                                    std::cout << " = array of size " << (pair.second.is_array() ? pair.second.size() : 0);
                                    if (pair.second.is_array()) {
                                        std::cout << " [";
                                        for (size_t i = 0; i < pair.second.size(); ++i) {
                                            if (i > 0) std::cout << ", ";
                                            const auto* v = pair.second.get(i);
                                            if (v) {
                                                if (v->is_string()) std::cout << "\"" << v->as_string() << "\"";
                                                else if (is_kv_int(v->type)) std::cout << v->as_int();
                                                else std::cout << "?";
                                            }
                                        }
                                        std::cout << "]";
                                    }
                                }
                                std::cout << std::endl;
                            }
                        }
                    }
                } else {
                    std::cerr << "[-] Failed to parse ResHeader." << std::endl;
                }
            } else {
                std::cerr << "[-] Failed to read model bytes." << std::endl;
            }
        }
    }

    std::cout << "\n[*] Loading BASE model (no defuser)..." << std::endl;
    auto mesh_base = VmdlParser::Load(target_model);
    if (!mesh_base.valid) {
        std::cerr << "[-] Failed to load base model. Error: " << VmdlParser::LastError() << std::endl;
        return 1;
    }
    std::cout << "[+] Base model loaded successfully!" << std::endl;
    std::cout << "  - Vertices count: " << mesh_base.vertices.size() << std::endl;
    std::cout << "  - Indices count:  " << mesh_base.indices.size() << std::endl;

    std::cout << "\n[*] Loading DEFUSER variant model..." << std::endl;
    auto mesh_def = VmdlParser::Load(target_model + "#defuser");
    if (!mesh_def.valid) {
        std::cerr << "[-] Failed to load defuser model. Error: " << VmdlParser::LastError() << std::endl;
        return 1;
    }
    std::cout << "[+] Defuser variant model loaded successfully!" << std::endl;
    std::cout << "  - Vertices count: " << mesh_def.vertices.size() << std::endl;
    std::cout << "  - Indices count:  " << mesh_def.indices.size() << std::endl;

    return 0;
}
