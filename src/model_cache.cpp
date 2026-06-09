#include "model_cache.hpp"
#include "renderer/gl_loader.hpp"
#include <iostream>
#include <algorithm>

// Model path key resolution matching the original overlay logic
static std::string model_key_from_path(const std::string_view path) {
    if (path.empty()) return {};

    std::string lowered(path);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    auto normalize_key = [](std::string key) -> std::string {
        if (auto vmdl = key.find(".vmdl"); vmdl != std::string::npos) {
            key.resize(vmdl);
        } else if (auto dot = key.find('.'); dot != std::string::npos) {
            key.resize(dot);
        }
        return key;
    };

    auto looks_like_agent_key = [](const std::string_view key) noexcept {
        return key.rfind("ctm_", 0) == 0 || key.rfind("tm_", 0) == 0 || key.rfind("ct_", 0) == 0;
    };

    std::string best_stem;
    size_t best_stem_len = 0;
    for (size_t i = 0; i < lowered.size(); ++i) {
        bool ctm = lowered.size() - i >= 4 && lowered.compare(i, 4, "ctm_") == 0;
        bool tm = !ctm && lowered.size() - i >= 3 && lowered.compare(i, 3, "tm_") == 0;
        if (!ctm && !tm) continue;

        size_t start = i;
        size_t end = start;
        while (end < lowered.size()) {
            unsigned char ch = lowered[end];
            if (!std::isalnum(ch) && lowered[end] != '_') break;
            ++end;
        }

        auto stem = normalize_key(lowered.substr(start, end - start));
        if (looks_like_agent_key(stem) && stem.size() >= 5 && stem.size() > best_stem_len) {
            best_stem_len = stem.size();
            best_stem = std::move(stem);
        }
        i = end > start ? end - 1 : end;
    }

    if (!best_stem.empty()) return best_stem;

    static const std::vector<std::string_view> k_model_path_markers = {
        "agents/models/", "agents/", "characters/models/", "characters/", "models/"
    };

    for (auto marker : k_model_path_markers) {
        auto marker_pos = lowered.find(marker);
        if (marker_pos == std::string::npos) continue;

        auto rest = lowered.substr(marker_pos + marker.size());
        auto slash = rest.find_last_of("/\\");
        auto candidate = slash != std::string::npos ? rest.substr(slash + 1) : rest;
        candidate = normalize_key(std::move(candidate));

        if (looks_like_agent_key(candidate)) return candidate;
    }

    auto end = lowered.size();
    while (end > 0 && (lowered[end - 1] == '/' || lowered[end - 1] == '\\')) --end;

    auto start = end;
    while (start > 0 && lowered[start - 1] != '/' && lowered[start - 1] != '\\') --start;

    return normalize_key(lowered.substr(start, end - start));
}

ModelCache::~ModelCache() {
    cleanup();
}

void ModelCache::cleanup() {
    for (auto& [key, item] : cache) {
        if (item.vbo) glDeleteBuffers(1, &item.vbo);
        if (item.ibo) glDeleteBuffers(1, &item.ibo);
        if (item.vao) glDeleteVertexArrays(1, &item.vao);
    }
    cache.clear();
    resolved_keys.clear();
}

std::string ModelCache::resolve_model_key(const std::string& path) {
    auto cache_it = resolved_keys.find(path);
    if (cache_it != resolved_keys.end()) {
        return cache_it->second;
    }

    std::string key = model_key_from_path(path);
    resolved_keys[path] = key;
    return key;
}

const CachedModel* ModelCache::get_or_load(const std::string& model_name) {
    if (model_name.empty()) return nullptr;

    std::string key = resolve_model_key(model_name);
    
    // Check main cache
    auto it = cache.find(key);
    if (it != cache.end()) {
        return &it->second;
    }

    // Try variants stripping if not found immediately
    if (key.find("_variant") != std::string::npos) {
        std::string base_key = key;
        auto variant_pos = base_key.find("_variant");
        base_key.resize(variant_pos);
        
        auto it_base = cache.find(base_key);
        if (it_base != cache.end()) {
            // Map the variant key to the base cached model to avoid reloading
            cache[key] = it_base->second;
            return &cache[key];
        }
    }

    // Load from VPK since it's not cached
    std::cout << "MODEL_CACHE: Loading model: " << model_name << " (resolved key: " << key << ")" << std::endl;
    
    AgentParser::AgentMesh mesh;
    bool success = AgentParser::LoadModel(model_name, mesh);
    
    if (!success && key.find("_variant") != std::string::npos) {
        // Fallback: try loading the base model directly from VPK
        std::string base_name = model_name;
        auto variant_pos = base_name.find("_variant");
        base_name.resize(variant_pos);
        base_name += ".vmdl";
        
        std::cout << "MODEL_CACHE: Variant load failed, trying base: " << base_name << std::endl;
        success = AgentParser::LoadModel(base_name, mesh);
    }

    CachedModel entry;
    if (success && mesh.valid && !mesh.vertices.empty()) {
        entry.mesh = std::move(mesh);
        entry.index_count = entry.mesh.indices.size();

        // Upload to GPU
        glGenVertexArrays(1, &entry.vao);
        glGenBuffers(1, &entry.vbo);
        glGenBuffers(1, &entry.ibo);

        glBindVertexArray(entry.vao);

        glBindBuffer(GL_ARRAY_BUFFER, entry.vbo);
        glBufferData(GL_ARRAY_BUFFER, entry.mesh.vertices.size() * sizeof(AgentParser::MeshVertex), entry.mesh.vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, entry.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, entry.mesh.indices.size() * sizeof(uint32_t), entry.mesh.indices.data(), GL_STATIC_DRAW);

        // Attribute 0: POSITION
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(AgentParser::MeshVertex), (void*)0);

        // Attribute 1: NORMAL
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(AgentParser::MeshVertex), (void*)(3 * sizeof(float)));

        // Attribute 2: TEXCOORD
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(AgentParser::MeshVertex), (void*)(6 * sizeof(float)));

        // Attribute 3: BLENDINDICES (Integer attribute!)
        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(3, 4, GL_UNSIGNED_SHORT, sizeof(AgentParser::MeshVertex), (void*)(8 * sizeof(float)));

        // Attribute 4: BLENDWEIGHT
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(AgentParser::MeshVertex), (void*)(8 * sizeof(float) + 4 * sizeof(uint16_t)));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        entry.valid = true;
        std::cout << "MODEL_CACHE: Loaded model successfully. Vertices: " << entry.mesh.vertices.size() << " Indices: " << entry.index_count << std::endl;
    } else {
        std::cerr << "MODEL_CACHE: Failed to load model: " << model_name << std::endl;
        entry.valid = false;
    }

    cache[key] = std::move(entry);
    return &cache[key];
}
