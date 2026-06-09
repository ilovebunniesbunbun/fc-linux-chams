#pragma once
#include <string>
#include <unordered_map>
#include "vpk/vmdl/model.hpp"

struct CachedModel {
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ibo = 0;
    size_t index_count = 0;
    bool valid = false;
    AgentParser::AgentMesh mesh;
};

class ModelCache {
public:
    ModelCache() = default;
    ~ModelCache();

    void cleanup();

    // Retrieves a cached model or loads it from VPK if not cached
    const CachedModel* get_or_load(const std::string& model_name);

private:
    std::unordered_map<std::string, CachedModel> cache;
    std::unordered_map<std::string, std::string> resolved_keys;

    // Helper to resolve model paths and strip variant suffixes
    std::string resolve_model_key(const std::string& path);
};
