#include "map_parser.hpp"
#include "../../vpk.hpp"
#include "../../source2.hpp"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <mutex>

namespace MapParser {

struct hedge_t {
    std::uint8_t next;
    std::uint8_t twin;
    std::uint8_t vert;
    std::uint8_t face;
};

static std::string NormalizeMapName(std::string mapName) {
    if (mapName.empty()) return {};
    for (char& c : mapName) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (const auto slash = mapName.find_last_of('/'); slash != std::string::npos)
        mapName = mapName.substr(slash + 1);
    if (mapName.size() > 4 && mapName.compare(mapName.size() - 4, 4, ".vpk") == 0)
        mapName.resize(mapName.size() - 4);
    if (mapName.size() > 4 && mapName.compare(mapName.size() - 4, 4, ".bsp") == 0)
        mapName.resize(mapName.size() - 4);
    if (mapName == "<empty>") return {};
    return mapName;
}

static bool OpenMapVpk(vpk::VPKDir& out_vpk,
                       const std::string& mapName,
                       std::string& opened_path) {
    for (const auto& pak_path : vpk::cs2_default_vpk_paths()) {
        const auto slash = pak_path.find_last_of('/');
        if (slash == std::string::npos) continue;
        const auto base_dir = pak_path.substr(0, slash);
        const auto candidate = base_dir + "/maps/" + mapName + ".vpk";
        if (out_vpk.open(candidate)) {
            opened_path = candidate;
            return true;
        }
    }

    for (const auto& container_path : vpk::find_workshop_map_container_vpks()) {
        vpk::VPKDir container_vpk;
        if (!container_vpk.open(container_path)) continue;

        const std::string nested_vpk_path = "maps/" + mapName + ".vpk";
        if (container_vpk.has_file(nested_vpk_path)) {
            auto nested_vpk_bytes = container_vpk.read_file(nested_vpk_path);
            if (nested_vpk_bytes && out_vpk.open_from_bytes(*nested_vpk_bytes)) {
                opened_path = container_path + ":" + nested_vpk_path;
                return true;
            }
        }

        const std::string physics_path = "maps/" + mapName + "/world_physics.vmdl_c";
        if (container_vpk.has_file(physics_path)) {
            if (out_vpk.open(container_path)) {
                opened_path = container_path;
                return true;
            }
        }
    }

    for (const auto& wp : vpk::find_workshop_map_vpks(mapName)) {
        if (out_vpk.open(wp)) {
            opened_path = wp;
            return true;
        }
    }

    return false;
}

static bool KvToBlobBytes(const source2::kv3::KVValue* v,
                           std::vector<uint8_t>& out) {
    if (!v) return false;
    const auto* blob = std::get_if<std::vector<uint8_t>>(&v->data);
    if (!blob || blob->empty()) return false;
    out = *blob;
    return true;
}

static bool KvToFloat3Array(const source2::kv3::KVValue* v,
                             std::vector<Vec3>& verts) {
    verts.clear();
    if (!v) return false;

    const auto* blob = std::get_if<std::vector<uint8_t>>(&v->data);
    if (!blob || blob->size() < 12 || (blob->size() % 12) != 0) return false;

    verts.reserve(blob->size() / 12);
    for (size_t i = 0; i + 12 <= blob->size(); i += 12) {
        const float x = source2::detail::rd<float>(blob->data() + i + 0);
        const float y = source2::detail::rd<float>(blob->data() + i + 4);
        const float z = source2::detail::rd<float>(blob->data() + i + 8);
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
        verts.push_back({x, y, z});
    }
    return !verts.empty();
}

static bool AppendHullFromPhysData(const std::vector<Vec3>& verts,
                                     const std::vector<uint8_t>& faces,
                                     const std::vector<hedge_t>& edges,
                                     std::vector<Triangle>& out,
                                     float type_id) {
    if (verts.size() < 3 || faces.empty() || edges.empty()) return false;

    const auto before = out.size();
    for (const auto start_he_u8 : faces) {
        const auto start_he = static_cast<size_t>(start_he_u8);
        if (start_he >= edges.size()) continue;

        std::vector<int> poly;
        poly.reserve(16);
        size_t he = start_he;
        int safety = 0;
        while (safety++ < 256) {
            if (he >= edges.size()) break;
            const auto vi = static_cast<int>(edges[he].vert);
            if (vi >= 0 && static_cast<size_t>(vi) < verts.size())
                poly.push_back(vi);
            he = static_cast<size_t>(edges[he].next);
            if (he == start_he) break;
        }
        if (poly.size() < 3) continue;

        const auto i0 = poly[0];
        for (size_t i = 1; i + 1 < poly.size(); ++i) {
            const auto i1 = poly[i];
            const auto i2 = poly[i + 1];
            if (i0 < 0 || i1 < 0 || i2 < 0) continue;
            if (static_cast<size_t>(i0) >= verts.size() ||
                static_cast<size_t>(i1) >= verts.size() ||
                static_cast<size_t>(i2) >= verts.size()) continue;
            out.push_back({{verts[static_cast<size_t>(i0)], type_id},
                           {verts[static_cast<size_t>(i1)], type_id},
                           {verts[static_cast<size_t>(i2)], type_id}});
        }
    }
    return out.size() > before;
}

static bool AppendMeshFromPhysData(const std::vector<Vec3>& verts,
                                     const std::vector<int32_t>& tris,
                                     std::vector<Triangle>& out,
                                     float type_id) {
    if (verts.size() < 3 || tris.size() < 3 || (tris.size() % 3) != 0) return false;

    const auto before = out.size();
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const auto i0 = tris[i + 0];
        const auto i1 = tris[i + 1];
        const auto i2 = tris[i + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0) continue;
        if (static_cast<size_t>(i0) >= verts.size() ||
            static_cast<size_t>(i1) >= verts.size() ||
            static_cast<size_t>(i2) >= verts.size()) continue;
        out.push_back({{verts[static_cast<size_t>(i0)], type_id},
                       {verts[static_cast<size_t>(i1)], type_id},
                       {verts[static_cast<size_t>(i2)], type_id}});
    }
    return out.size() > before;
}

bool AppendPhysBlockTriangles(const std::vector<uint8_t>& vmdl_blob,
                              std::vector<Triangle>& out,
                              std::vector<Triangle>& out_visual,
                              float type_id) {
    auto hdr_opt = source2::parse_res_header(vmdl_blob.data(), vmdl_blob.size());
    if (!hdr_opt) return false;

    const auto* phys_blk = source2::find_block(*hdr_opt, "PHYS");
    if (!phys_blk || phys_blk->offset + phys_blk->size > vmdl_blob.size())
        return false;

    auto kv_opt = source2::kv3::parse_binary(
        vmdl_blob.data() + phys_blk->offset, phys_blk->size);
    if (!kv_opt || !kv_opt->is_object()) return false;

    auto get_first = [](const source2::kv3::KVValue* obj,
                        std::initializer_list<const char*> keys)
        -> const source2::kv3::KVValue* {
        if (!obj || !obj->is_object()) return nullptr;
        for (const auto* k : keys)
            if (const auto* v = obj->get(k)) return v;
        return nullptr;
    };

    const auto* collision_attrs_root = get_first(&*kv_opt, { "m_collisionAttributes", "m_CollisionAttributes" });
    if (!collision_attrs_root || !collision_attrs_root->is_array()) {
        collision_attrs_root = nullptr;
    }

    auto ShouldSkipPhysicsGroup = [&get_first, collision_attrs_root](const source2::kv3::KVValue* shape, bool for_visuals) -> bool {
        if (!collision_attrs_root) return false;
        if (!shape || !shape->is_object()) return false;

        const auto* attr_idx = get_first(shape, { "m_nCollisionAttributeIndex", "m_collisionAttributeIndex" });
        if (!attr_idx) return false;

        const int idx = static_cast<int>(attr_idx->as_int());
        if (idx < 0 || static_cast<size_t>(idx) >= collision_attrs_root->size()) return false;

        const auto* attr = collision_attrs_root->get(idx);
        if (!attr || !attr->is_object()) return false;

        // Skip skybox, player clips, and npc clips
        const auto* interact_as = get_first(attr, { "m_InteractAsStrings", "m_interactAsStrings" });
        if (interact_as && interact_as->is_array()) {
            for (size_t i = 0; i < interact_as->size(); ++i) {
                const auto* val = interact_as->get(i);
                if (val && val->is_string()) {
                    std::string tag = val->as_string();
                    std::transform(tag.begin(), tag.end(), tag.begin(), ::tolower);
                    if (tag == "sky" || tag == "playerclip" || tag == "npcclip") {
                        return true;
                    }
                }
            }
        }

        if (for_visuals) {
            // Visuals do not care about the collision group (debris, weapons, etc. should block chams)
            return false;
        }

        const auto* collision_group = get_first(attr, { "m_CollisionGroupString", "m_collisionGroupString" });
        if (!collision_group || !collision_group->is_string()) return false;

        const auto group_str = collision_group->as_string();

        std::string group_lower = group_str;
        std::transform(group_lower.begin(), group_lower.end(), group_lower.begin(), ::tolower);
        return (group_lower != "default" && 
                group_lower != "conditionallysolid" && 
                group_lower != "collision_group_debris_block_projectile");
    };

    const auto* parts = get_first(&*kv_opt, {"m_parts", "m_Parts", "parts", "Parts"});
    if (!parts || !parts->is_array()) {
        const auto* phys_obj = get_first(
            &*kv_opt, {"m_physData", "m_PhysData", "m_pData", "m_data"});
        parts = get_first(phys_obj, {"m_parts", "m_Parts", "parts", "Parts"});
    }
    if (!parts || !parts->is_array() || parts->size() == 0) return false;

    std::size_t parsed_hulls = 0;
    std::size_t parsed_meshes = 0;

    for (size_t pi = 0; pi < parts->size(); ++pi) {
        const auto* part = parts->get(pi);
        if (!part || !part->is_object()) continue;

        const auto* rn_shape = get_first(part, {"m_rnShape", "m_RnShape", "m_shape"});
        if (!rn_shape || !rn_shape->is_object()) continue;

        if (const auto* hulls = get_first(rn_shape, {"m_hulls", "m_Hulls"});
            hulls && hulls->is_array()) {
            for (size_t hi = 0; hi < hulls->size(); ++hi) {
                const auto* h = hulls->get(hi);
                if (!h || !h->is_object()) continue;

                bool pass_physics = !ShouldSkipPhysicsGroup(h, false);
                bool pass_visual = !ShouldSkipPhysicsGroup(h, true);
                if (!pass_physics && !pass_visual) continue;

                const auto* hull = get_first(h, {"m_Hull", "m_hull"});
                if (!hull || !hull->is_object()) hull = h;
                if (!hull || !hull->is_object()) continue;

                const auto* vpos = get_first(hull,
                    {"m_VertexPositions", "m_vertexPositions", "m_Vertices", "m_vertices"});
                std::vector<Vec3> verts;
                if (!KvToFloat3Array(vpos, verts)) continue;

                std::vector<uint8_t> faces_raw;
                if (!KvToBlobBytes(get_first(hull, {"m_Faces", "m_faces"}), faces_raw))
                    continue;

                std::vector<uint8_t> edges_raw;
                if (!KvToBlobBytes(get_first(hull, {"m_Edges", "m_edges"}), edges_raw) ||
                    (edges_raw.size() % 4) != 0)
                    continue;

                std::vector<hedge_t> edges(edges_raw.size() / 4);
                for (size_t ei = 0; ei < edges.size(); ++ei)
                    edges[ei] = {edges_raw[ei * 4 + 0], edges_raw[ei * 4 + 1],
                                 edges_raw[ei * 4 + 2], edges_raw[ei * 4 + 3]};

                std::vector<Triangle> temp_tris;
                if (AppendHullFromPhysData(verts, faces_raw, edges, temp_tris, type_id)) {
                    if (pass_physics) {
                        out.insert(out.end(), temp_tris.begin(), temp_tris.end());
                    }
                    if (pass_visual) {
                        out_visual.insert(out_visual.end(), temp_tris.begin(), temp_tris.end());
                    }
                    ++parsed_hulls;
                }
            }
        }
        if (const auto* meshes = get_first(rn_shape, {"m_meshes", "m_Meshes"});
            meshes && meshes->is_array()) {
            for (size_t mi = 0; mi < meshes->size(); ++mi) {
                const auto* m = meshes->get(mi);
                if (!m || !m->is_object()) continue;

                bool pass_physics = !ShouldSkipPhysicsGroup(m, false);
                bool pass_visual = !ShouldSkipPhysicsGroup(m, true);
                if (!pass_physics && !pass_visual) continue;

                const auto* mesh = get_first(m, {"m_Mesh", "m_mesh"});
                if (!mesh || !mesh->is_object()) mesh = m;
                if (!mesh || !mesh->is_object()) continue;

                std::vector<Vec3> verts;
                if (!KvToFloat3Array(get_first(mesh,
                    {"m_Vertices", "m_vertices", "m_VertexPositions", "m_vertexPositions"}),
                    verts)) continue;

                std::vector<int32_t> tris_int32;
                std::vector<uint8_t> tris_raw;
                if (!KvToBlobBytes(get_first(mesh, { "m_Triangles", "m_Triangles" }), tris_raw) || (tris_raw.size() % 12) != 0)
                    continue;

                tris_int32.reserve(tris_raw.size() / 4);
                for (size_t i = 0; i < tris_raw.size(); i += 4) {
                    tris_int32.push_back(source2::detail::rd<int32_t>(tris_raw.data() + i));
                }

                std::vector<Triangle> temp_tris;
                if (!tris_int32.empty() && AppendMeshFromPhysData(verts, tris_int32, temp_tris, type_id)) {
                    if (pass_physics) {
                        out.insert(out.end(), temp_tris.begin(), temp_tris.end());
                    }
                    if (pass_visual) {
                        out_visual.insert(out_visual.end(), temp_tris.begin(), temp_tris.end());
                    }
                    ++parsed_meshes;
                }
            }
        }
    }

    return parsed_hulls > 0 || parsed_meshes > 0;
}

static bool BuildFromWorldPhysics(vpk::VPKDir& map_vpk,
                                  const std::string& mapName,
                                  std::vector<Triangle>& out,
                                  std::vector<Triangle>& out_visual) {
    const std::string phys_path_vrman = "maps/" + mapName + "/world_physics.vrman_c";
    auto bytes_vrman = map_vpk.read_file(phys_path_vrman);
    if (bytes_vrman && !bytes_vrman->empty()) {
        if (AppendPhysBlockTriangles(*bytes_vrman, out, out_visual))
            return true;
    }

    const std::string phys_path_vmdl = "maps/" + mapName + "/world_physics.vmdl_c";
    auto bytes_vmdl = map_vpk.read_file(phys_path_vmdl);
    if (bytes_vmdl && !bytes_vmdl->empty()) {
        if (AppendPhysBlockTriangles(*bytes_vmdl, out, out_visual))
            return true;
    }

    return false;
}

static Vec3 ParseVec3(const source2::kv3::KVValue* val, const Vec3& default_val = {0.f, 0.f, 0.f}) {
    if (!val) return default_val;
    if (val->is_array() && val->size() >= 3) {
        float x = 0.f, y = 0.f, z = 0.f;
        if (const auto* xv = val->get(0)) x = static_cast<float>(xv->as_float());
        if (const auto* yv = val->get(1)) y = static_cast<float>(yv->as_float());
        if (const auto* zv = val->get(2)) z = static_cast<float>(zv->as_float());
        return {x, y, z};
    }
    if (val->is_string()) {
        std::stringstream ss(val->as_string());
        float x = default_val.x, y = default_val.y, z = default_val.z;
        if (ss >> x >> y >> z) return {x, y, z};
    }
    return default_val;
}

static void IntegrateEntityHulls(vpk::VPKDir& map_vpk,
                                 const std::string& mapName,
                                 std::vector<Triangle>& out,
                                 std::vector<Triangle>& out_visual) {
    const std::string ents_path = "maps/" + mapName + "/entities/default_ents.vents_c";
    auto bytes_ents = map_vpk.read_file(ents_path);
    if (!bytes_ents || bytes_ents->empty()) return;

    auto hdr_opt = source2::parse_res_header(bytes_ents->data(), bytes_ents->size());
    if (!hdr_opt) return;

    const auto* data_blk = source2::find_block(*hdr_opt, "DATA");
    if (!data_blk || data_blk->offset + data_blk->size > bytes_ents->size()) return;

    auto kv_opt = source2::kv3::parse_binary(
        bytes_ents->data() + data_blk->offset, data_blk->size);
    if (!kv_opt || !kv_opt->is_object()) return;

    const auto* entities_val = kv_opt->get("m_entityKeyValues");
    if (!entities_val || !entities_val->is_array()) return;

    static vpk::VPKDir s_pak01_vpk;
    static bool s_pak01_vpk_initialized = false;
    static std::mutex s_pak01_mutex;

    if (!s_pak01_vpk_initialized) {
        std::lock_guard<std::mutex> lock(s_pak01_mutex);
        if (!s_pak01_vpk_initialized) {
            for (const auto& path : vpk::cs2_default_vpk_paths()) {
                if (s_pak01_vpk.open(path)) {
                    s_pak01_vpk_initialized = true;
                    break;
                }
            }
        }
    }

    for (size_t i = 0; i < entities_val->size(); ++i) {
        const auto* ent = entities_val->get(i);
        if (!ent || !ent->is_object()) continue;

        const auto* kv3_data = ent->get("keyValues3Data");
        if (!kv3_data || !kv3_data->is_object()) continue;

        const auto* values = kv3_data->get("values");
        if (!values || !values->is_object()) continue;

        const auto* classname_val = values->get("classname");
        if (!classname_val || !classname_val->is_string()) continue;
        std::string classname = classname_val->as_string();

        if (classname != "func_brush" && classname != "func_breakable" &&
            classname != "func_clip_vphysics" && classname != "func_physbox" &&
            classname != "prop_physics" && classname != "prop_physics_multiplayer") {
            continue;
        }

        float type_id = 0.0f;
        if (classname == "func_breakable") type_id = 1.0f;
        else if (classname == "prop_physics" || classname == "prop_physics_multiplayer") type_id = 2.0f;
        else if (classname == "func_brush") type_id = 3.0f;
        else if (classname == "func_clip_vphysics") type_id = 4.0f;
        else if (classname == "func_physbox") type_id = 5.0f;
        else type_id = 6.0f;

        const auto* model_val = values->get("model");
        if (!model_val || !model_val->is_string()) continue;
        std::string model_path = model_val->as_string();
        if (model_path.empty()) continue;

        if (model_path.size() >= 5 && model_path.compare(model_path.size() - 5, 5, ".vmdl") == 0) {
            model_path += "_c";
        }
        for (char& c : model_path) {
            if (c == '\\') c = '/';
            else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }

        std::optional<std::vector<uint8_t>> model_bytes = map_vpk.read_file(model_path);
        if (!model_bytes && s_pak01_vpk_initialized) {
            model_bytes = s_pak01_vpk.read_file(model_path);
        }
        if (!model_bytes) continue;

        std::vector<Triangle> local_phys;
        std::vector<Triangle> local_visual;
        if (!AppendPhysBlockTriangles(*model_bytes, local_phys, local_visual, type_id)) continue;

        Vec3 origin = ParseVec3(values->get("origin"), {0.f, 0.f, 0.f});
        Vec3 angles = ParseVec3(values->get("angles"), {0.f, 0.f, 0.f});
        Vec3 scales = ParseVec3(values->get("scales"), {1.f, 1.f, 1.f});
        if (const auto* scale_val = values->get("scale")) {
            float s = static_cast<float>(scale_val->as_float());
            scales = {s, s, s};
        }

        const float pitch_rad = angles.x * (3.14159265f / 180.f);
        const float yaw_rad   = angles.y * (3.14159265f / 180.f);
        const float roll_rad  = angles.z * (3.14159265f / 180.f);

        float cx = std::cos(roll_rad); float sx = std::sin(roll_rad);
        float cy = std::cos(pitch_rad); float sy = std::sin(pitch_rad);
        float cz = std::cos(yaw_rad); float sz = std::sin(yaw_rad);

        float r00 = cz * cy; float r01 = cz * sx * sy - sz * cx; float r02 = cz * cx * sy + sz * sx;
        float r10 = sz * cy; float r11 = sz * sx * sy + cz * cx; float r12 = sz * cx * sy - cz * sx;
        float r20 = -sy; float r21 = sx * cy; float r22 = cx * cy;

        auto TransformVec3 = [&](const Vec3& v) -> Vec3 {
            float sx_v = v.x * scales.x;
            float sy_v = v.y * scales.y;
            float sz_v = v.z * scales.z;
            float rx_v = r00 * sx_v + r01 * sy_v + r02 * sz_v;
            float ry_v = r10 * sx_v + r11 * sy_v + r12 * sz_v;
            float rz_v = r20 * sx_v + r21 * sy_v + r22 * sz_v;
            return {rx_v + origin.x, ry_v + origin.y, rz_v + origin.z};
        };

        auto TransformTriangle = [&](const Triangle& tri) -> Triangle {
            return {{TransformVec3(tri.v0.pos), tri.v0.type_id}, {TransformVec3(tri.v1.pos), tri.v1.type_id}, {TransformVec3(tri.v2.pos), tri.v2.type_id}};
        };

        bool add_to_geometry = true;
        if (classname == "func_clip_vphysics" || classname == "func_physbox") {
            add_to_geometry = false;
        }
        if (classname == "func_brush") {
            const source2::kv3::KVValue* sd_val = nullptr;
            for (const auto& [k, v] : values->as_object()) {
                std::string k_lower = k;
                std::transform(k_lower.begin(), k_lower.end(), k_lower.begin(), ::tolower);
                if (k_lower == "startdisabled") {
                    sd_val = &v;
                    break;
                }
            }
            if (sd_val) {
                bool is_disabled = false;
                if (sd_val->type == source2::kv3::KVType::Boolean_true) is_disabled = true;
                else if (sd_val->type == source2::kv3::KVType::Int64 && sd_val->as_int() != 0) is_disabled = true;
                else if (sd_val->type == source2::kv3::KVType::UInt64 && sd_val->as_int() != 0) is_disabled = true;
                else if (sd_val->type == source2::kv3::KVType::Double && sd_val->as_float() != 0.0) is_disabled = true;
                else if (sd_val->is_string()) {
                    std::string s = sd_val->as_string();
                    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
                    if (s == "1" || s == "true") is_disabled = true;
                }
                if (is_disabled) {
                    add_to_geometry = false;
                }
            }
        }

        if (add_to_geometry) {
            for (const auto& tri : local_phys) out.push_back(TransformTriangle(tri));
            for (const auto& tri : local_visual) out_visual.push_back(TransformTriangle(tri));
        }
    }
}

std::string GetCurrentMap() {
    // Retained for interface compatibility, maps are supplied via SHM bridge
    return {};
}

std::vector<MapEntry> ListAllMaps() {
    std::vector<MapEntry> result;
    
    for (const auto& pak_path : vpk::cs2_default_vpk_paths()) {
        const auto slash = pak_path.find_last_of('/');
        if (slash == std::string::npos) continue;
        const auto base_dir = pak_path.substr(0, slash);
        
        std::string maps_dir = base_dir + "/maps/";
        
        try {
            for (const auto& entry : std::filesystem::directory_iterator(maps_dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".vpk") {
                    std::string filename = entry.path().filename().string();
                    size_t dot = filename.rfind('.');
                    if (dot != std::string::npos) filename = filename.substr(0, dot);
                    
                    MapEntry map_entry;
                    map_entry.Name = filename;
                    map_entry.Path = "maps/" + filename + "/world_physics.vmdl_c";
                    result.push_back(map_entry);
                }
            }
        } catch (...) {}
        
        if (!result.empty()) break;
    }
    
    auto workshop_vpks = vpk::find_workshop_map_container_vpks();
    for (const auto& container_path : workshop_vpks) {
        vpk::VPKDir container_vpk;
        if (!container_vpk.open(container_path)) continue;
        
        auto nested_vpks = container_vpk.list_files("maps/", ".vpk");
        for (const auto& nested_path : nested_vpks) {
            std::string map_name = nested_path;
            size_t maps_slash = map_name.find("maps/");
            if (maps_slash != std::string::npos) {
                map_name = map_name.substr(maps_slash + 5);
            }
            size_t dot = map_name.rfind('.');
            if (dot != std::string::npos) map_name = map_name.substr(0, dot);
            
            MapEntry map_entry;
            map_entry.Name = map_name;
            map_entry.Path = "maps/" + map_name + "/world_physics.vmdl_c";
            result.push_back(map_entry);
        }
    }
    
    for (const auto& workshop_vpk : vpk::find_workshop_map_vpks("")) {
        std::string filename = workshop_vpk;
        size_t slash = filename.rfind('/');
        if (slash != std::string::npos) filename = filename.substr(slash + 1);
        size_t dot = filename.rfind('.');
        if (dot != std::string::npos) filename = filename.substr(0, dot);
        
        MapEntry map_entry;
        map_entry.Name = filename;
        map_entry.Path = "maps/" + filename + "/world_physics.vmdl_c";
        result.push_back(map_entry);
    }
    
    return result;
}

static std::string s_LoadStatus;
static MapMesh s_LoadedMesh;
std::string GetLoadStatus() { return s_LoadStatus; }

MapMesh LoadMesh(const std::string& mapName) {
    MapMesh result;
    const std::string normalized = NormalizeMapName(mapName);
    if (normalized.empty()) {
        s_LoadedMesh = {};
        return result;
    }

    vpk::VPKDir map_vpk;
    std::string opened_path;
    if (!OpenMapVpk(map_vpk, normalized, opened_path)) {
        s_LoadStatus = "vpk not found for: " + normalized;
        s_LoadedMesh = {};
        return result;
    }

    if (BuildFromWorldPhysics(map_vpk, normalized, result.Triangles, result.VisualTriangles)) {
        result.Valid = true;
        IntegrateEntityHulls(map_vpk, normalized, result.Triangles, result.VisualTriangles);
        s_LoadStatus = "ok (world_physics tris=" +
                        std::to_string(result.Triangles.size()) + ", visual=" +
                        std::to_string(result.VisualTriangles.size()) + ") from " + opened_path;
        s_LoadedMesh = result;
        return result;
    }

    s_LoadStatus = "no geometry found for: " + normalized;
    s_LoadedMesh = {};
    return result;
}

const MapMesh* GetLoadedMesh() {
    if (!s_LoadedMesh.Valid || s_LoadedMesh.Triangles.empty()) return nullptr;
    return &s_LoadedMesh;
}

void ClearLoadedMesh() {
    s_LoadedMesh = {};
}

std::vector<Vec2> ProjectTopDown(const MapMesh& mesh,
                                 float canvasW, float canvasH) {
    static constexpr std::size_t k_max_segments = 200000;
    if (!mesh.Valid || mesh.Triangles.empty()) return {};

    float min_x = mesh.Triangles[0].v0.pos.x, max_x = min_x;
    float min_y = mesh.Triangles[0].v0.pos.y, max_y = min_y;

    auto update_bounds = [&](const Vec3& v) {
        if (!std::isfinite(v.x) || !std::isfinite(v.y)) return;
        min_x = std::min(min_x, v.x); max_x = std::max(max_x, v.x);
        min_y = std::min(min_y, v.y); max_y = std::max(max_y, v.y);
    };
    for (const auto& tri : mesh.Triangles) {
        update_bounds(tri.v0.pos);
        update_bounds(tri.v1.pos);
        update_bounds(tri.v2.pos);
    }

    const float rx = max_x - min_x, ry = max_y - min_y;
    if (rx < 1.f || ry < 1.f) return {};

    const float scale = std::min(canvasW / rx, canvasH / ry) * 0.95f;

    auto proj = [&](const Vec3& v) -> Vec2 {
        return {(v.x - min_x) * scale, canvasH - (v.y - min_y) * scale};
    };

    std::vector<Vec2> lines;
    lines.reserve(std::min(mesh.Triangles.size() * 6, k_max_segments * 2));

    for (const auto& tri : mesh.Triangles) {
        if (lines.size() / 2 >= k_max_segments) break;

        if (!std::isfinite(tri.v0.pos.x) || !std::isfinite(tri.v0.pos.y) ||
            !std::isfinite(tri.v1.pos.x) || !std::isfinite(tri.v1.pos.y) ||
            !std::isfinite(tri.v2.pos.x) || !std::isfinite(tri.v2.pos.y))
            continue;

        const Vec2 pa = proj(tri.v0.pos), pb = proj(tri.v1.pos), pc = proj(tri.v2.pos);

        lines.push_back(pa); lines.push_back(pb);
        lines.push_back(pb); lines.push_back(pc);
        lines.push_back(pc); lines.push_back(pa);
    }

    return lines;
}

}
