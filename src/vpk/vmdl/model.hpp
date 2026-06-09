#pragma once

#include "../source2.hpp"
#include "../vpk.hpp"

#include <vector>
#include <array>
#include <string>
#include <cstdint>

namespace AgentParser {

struct MeshVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    std::uint16_t joints[4];
    float weights[4];
};

struct AgentMesh {
    std::vector<MeshVertex>      vertices;
    std::vector<std::uint32_t>   indices;
    std::vector<source2::Mat3x4> inv_bind_poses;
    std::vector<std::int16_t>    bone_parents;
    int  bone_count = 0;
    bool valid      = false;
};

bool LoadModel(const std::string& ModelPath, AgentMesh& Out);
bool LoadModelFromBytes(const std::vector<uint8_t>& Bytes, vpk::VPKDir& Vpk, AgentMesh& Out);
bool EnsureVpkOpen();
void SetCustomVpkPath(const std::string& path);

}
