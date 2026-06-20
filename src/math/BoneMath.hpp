#pragma once

#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <algorithm>
#include "vpk/source2.hpp"
#include "vpk/vmdl/model.hpp"

inline source2::Mat3x4 matrix_from_bone(const glm::vec3& pos, const glm::vec4& rot) {
    glm::quat gq(rot.w, rot.x, rot.y, rot.z);
    glm::mat3 rot_mat = glm::mat3_cast(gq);
    return source2::Mat3x4(
        glm::vec4(rot_mat[0], pos.x),
        glm::vec4(rot_mat[1], pos.y),
        glm::vec4(rot_mat[2], pos.z)
    );
}

inline source2::Mat3x4 multiply_3x4(const source2::Mat3x4& a, const source2::Mat3x4& b) {
    return source2::detail::mat_mul(a, b);
}

inline void sanitize_lod_bones(source2::Mat3x4* bones, int bone_count, const AgentParser::AgentMesh& mesh) {
    const int num_ibm = static_cast<int>(mesh.inv_bind_poses.size());
    const int num_parents = static_cast<int>(mesh.bone_parents.size());
    if (num_parents == 0 || num_ibm == 0) return;

    const int limit = (std::min)({ bone_count, num_ibm, num_parents });
    if (limit <= 28) return;

    const float org_x = bones[0][0][3];
    const float org_y = bones[0][1][3];
    const float org_z = bones[0][2][3];
    const float org_dist_sq = org_x * org_x + org_y * org_y + org_z * org_z;
 
    auto bind_pos = [&](int idx, float& bx, float& by, float& bz) {
        const auto& m = mesh.inv_bind_poses[idx];
        const float tx = m[0][3], ty = m[1][3], tz = m[2][3];
        bx = -(m[0][0] * tx + m[0][1] * ty + m[0][2] * tz);
        by = -(m[1][0] * tx + m[1][1] * ty + m[1][2] * tz);
        bz = -(m[2][0] * tx + m[2][1] * ty + m[2][2] * tz);
    };
 
    std::vector<uint8_t> exploded(limit, 0);
 
    for (int i = 28; i < limit; ++i) {
        const int16_t p = mesh.bone_parents[i];
        if (p < 0 || p == i || p >= limit) continue;
 
        float bi_x, bi_y, bi_z, bp_x, bp_y, bp_z;
        bind_pos(i, bi_x, bi_y, bi_z);
        bind_pos(p, bp_x, bp_y, bp_z);
        const float dd_x = bi_x - bp_x, dd_y = bi_y - bp_y, dd_z = bi_z - bp_z;
 
        const auto& ibm_p = mesh.inv_bind_poses[p];
        const float lx = ibm_p[0][0] * dd_x + ibm_p[1][0] * dd_y + ibm_p[2][0] * dd_z;
        const float ly = ibm_p[0][1] * dd_x + ibm_p[1][1] * dd_y + ibm_p[2][1] * dd_z;
        const float lz = ibm_p[0][2] * dd_x + ibm_p[1][2] * dd_y + ibm_p[2][2] * dd_z;
 
        const auto& gp = bones[p];
        const float wx = gp[0][0] * lx + gp[1][0] * ly + gp[2][0] * lz;
        const float wy = gp[0][1] * lx + gp[1][1] * ly + gp[2][1] * lz;
        const float wz = gp[0][2] * lx + gp[1][2] * ly + gp[2][2] * lz;
 
        const float recon_x = gp[0][3] + wx;
        const float recon_y = gp[1][3] + wy;
        const float recon_z = gp[2][3] + wz;
 
        bool garbage = false;
 
        if (exploded[p]) {
            garbage = true;
        }
 
        if (!garbage) {
            const float dx = bones[i][0][3] - org_x;
            const float dy = bones[i][1][3] - org_y;
            const float dz = bones[i][2][3] - org_z;
            if (dx * dx + dy * dy + dz * dz > 120.0f * 120.0f)
                garbage = true;
        }
 
        if (!garbage) {
            const float dx = bones[i][0][3] - recon_x;
            const float dy = bones[i][1][3] - recon_y;
            const float dz = bones[i][2][3] - recon_z;
            if (dx * dx + dy * dy + dz * dz > 60.0f * 60.0f)
                garbage = true;
        }
 
        if (!garbage) {
            const float bx = bones[i][0][3], by = bones[i][1][3], bz = bones[i][2][3];
            if (org_dist_sq > 500.0f * 500.0f && (bx * bx + by * by + bz * bz) < 50.0f * 50.0f)
                garbage = true;
        }
 
        if (garbage) {
            bones[i] = bones[p];
            bones[i][0][3] = recon_x;
            bones[i][1][3] = recon_y;
            bones[i][2][3] = recon_z;
            exploded[i] = 1;
        }
    }
}
