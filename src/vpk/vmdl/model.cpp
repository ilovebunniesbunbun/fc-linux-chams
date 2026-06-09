#include "model.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace AgentParser {

static vpk::VPKDir s_VpkDir;

static void RecalculateNormalsFromIndices(std::vector<MeshVertex>& Verts, const std::vector<std::uint32_t>& Idxs) {
    for (auto& V : Verts) { V.nx = 0; V.ny = 0; V.nz = 0; }

    for (size_t I = 0; I + 2 < Idxs.size(); I += 3) {
        const auto I0 = Idxs[I], I1 = Idxs[I + 1], I2 = Idxs[I + 2];
        if (I0 >= Verts.size() || I1 >= Verts.size() || I2 >= Verts.size()) continue;
        const auto& P0 = Verts[I0]; const auto& P1 = Verts[I1]; const auto& P2 = Verts[I2];
        const float E1x = P1.px - P0.px, E1y = P1.py - P0.py, E1z = P1.pz - P0.pz;
        const float E2x = P2.px - P0.px, E2y = P2.py - P0.py, E2z = P2.pz - P0.pz;

        const float Cx = E1y * E2z - E1z * E2y;
        const float Cy = E1z * E2x - E1x * E2z;
        const float Cz = E1x * E2y - E1y * E2x;
        Verts[I0].nx += Cx; Verts[I0].ny += Cy; Verts[I0].nz += Cz;
        Verts[I1].nx += Cx; Verts[I1].ny += Cy; Verts[I1].nz += Cz;
        Verts[I2].nx += Cx; Verts[I2].ny += Cy; Verts[I2].nz += Cz;
    }

    for (auto& V : Verts) {
        const float Len = std::sqrt(V.nx * V.nx + V.ny * V.ny + V.nz * V.nz);
        if (Len > 1e-6f) { V.nx /= Len; V.ny /= Len; V.nz /= Len; }
        else { V.nx = 0; V.ny = 1.0f; V.nz = 0; }
    }
}

static void SmoothNormals(std::vector<MeshVertex>& Verts) {
    constexpr float EPS = 0.001f;
    constexpr float EPS2 = EPS * EPS;
    const size_t N = Verts.size();
    if (N == 0) return;

    std::vector<std::uint32_t> Sorted(N);
    std::iota(Sorted.begin(), Sorted.end(), 0u);
    std::sort(Sorted.begin(), Sorted.end(), [&](std::uint32_t A, std::uint32_t B) {
        if (Verts[A].px != Verts[B].px) return Verts[A].px < Verts[B].px;
        if (Verts[A].py != Verts[B].py) return Verts[A].py < Verts[B].py;
        return Verts[A].pz < Verts[B].pz;
    });

    std::vector<float> Snx(N), Sny(N), Snz(N);
    for (size_t I = 0; I < N; ++I) {
        const auto Vi = Sorted[I];
        float Ax = Verts[Vi].nx, Ay = Verts[Vi].ny, Az = Verts[Vi].nz;

        for (size_t J = I + 1; J < N; ++J) {
            const auto Vj = Sorted[J];
            if (Verts[Vj].px - Verts[Vi].px > EPS) break;
            const float Dx = Verts[Vj].px - Verts[Vi].px;
            const float Dy = Verts[Vj].py - Verts[Vi].py;
            const float Dz = Verts[Vj].pz - Verts[Vi].pz;
            if (Dx * Dx + Dy * Dy + Dz * Dz < EPS2) {
                Ax += Verts[Vj].nx; Ay += Verts[Vj].ny; Az += Verts[Vj].nz;
            }
        }

        const float Len = std::sqrt(Ax * Ax + Ay * Ay + Az * Az);
        if (Len > 1e-6f) { Snx[Vi] = Ax / Len; Sny[Vi] = Ay / Len; Snz[Vi] = Az / Len; }
        else { Snx[Vi] = 0; Sny[Vi] = 1.0f; Snz[Vi] = 0; }
    }

    for (size_t I = 0; I < N; ++I) {
        Verts[I].nx = Snx[I]; Verts[I].ny = Sny[I]; Verts[I].nz = Snz[I];
    }
}

static std::string NormalizePath(std::string Path, const char* SrcExt, const char* CmpExt) {
    for (char& C : Path) {
        if (C == '\\') C = '/';
        else if (C >= 'A' && C <= 'Z') C = static_cast<char>(C - 'A' + 'a');
    }
    const std::string Se = SrcExt;
    const std::string Ce = CmpExt;
    if (Path.size() >= Ce.size() && Path.compare(Path.size() - Ce.size(), Ce.size(), Ce) == 0)
        return Path;
    if (Path.size() >= Se.size() && Path.compare(Path.size() - Se.size(), Se.size(), Se) == 0) {
        Path += "_c";
        return Path;
    }
    return Path + Ce;
}

static bool ResolveGeometryFromRefs(vpk::VPKDir& Dir, source2::ModelData& Md) {
    if (Md.has_geometry()) return true;

    for (const auto& Ref : Md.mesh_resources) {
        const std::string MeshPath = NormalizePath(Ref, ".vmesh", ".vmesh_c");
        auto Bytes = Dir.read_file(MeshPath);
        if (!Bytes) continue;

        auto VbibOpt = source2::parse_vmesh_c(Bytes->data(), Bytes->size());
        if (!VbibOpt || VbibOpt->vbs.empty() || VbibOpt->ibs.empty())
            continue;

        auto VmdlRemap  = std::move(Md.remapping_table);
        auto VmdlStarts = std::move(Md.remapping_table_starts);
        Md.remapping_table.clear();
        Md.remapping_table_starts.clear();

        source2::extract_vmesh_c_remap(Bytes->data(), Bytes->size(), Md);

        if (Md.remapping_table.empty() && !VmdlRemap.empty()) {
            Md.remapping_table        = std::move(VmdlRemap);
            Md.remapping_table_starts = std::move(VmdlStarts);
        }

        Md.vertex_buffers = std::move(VbibOpt->vbs);
        Md.index_buffers  = std::move(VbibOpt->ibs);
        Md.geometry_source = 3;
        return true;
    }
    return false;
}

static bool BuildMesh(const source2::ModelData& Md, AgentMesh& Out) {
    Out = {};
    if (!Md.valid()) return false;

    const size_t SkelSize = Md.skeleton.size();
    Out.bone_count = static_cast<int>(SkelSize);

    Out.inv_bind_poses.resize(SkelSize);
    for (size_t I = 0; I < SkelSize; ++I) {
        if (I < Md.inv_bind_poses.size()) {
            Out.inv_bind_poses[I] = Md.inv_bind_poses[I];
        } else {
            auto& M = Out.inv_bind_poses[I];
            M = {};
            M.mat[0][0] = M.mat[1][1] = M.mat[2][2] = 1.0f;
        }
    }

    Out.bone_parents.resize(SkelSize, -1);
    for (size_t I = 0; I < SkelSize; ++I) {
        Out.bone_parents[I] = static_cast<std::int16_t>(Md.skeleton[I].parent);
    }

    auto IsUnstable = [&](int Bi) -> bool {
        if (Bi < 0 || Bi >= static_cast<int>(SkelSize)) return false;
        std::string Lower = Md.skeleton[Bi].name;
        std::transform(Lower.begin(), Lower.end(), Lower.begin(),
                       [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
        return Lower.find("jiggle") != std::string::npos ||
               Lower.find("iktarget") != std::string::npos ||
               Lower.find("weaponhier") != std::string::npos ||
               Lower.find("attach") != std::string::npos;
    };

    auto CollapseToStable = [&](int Bi) -> int {
        int Depth = 0;
        while (IsUnstable(Bi) && Depth < 10) {
            int Parent = Md.skeleton[Bi].parent;
            if (Parent < 0 || Parent == Bi) break;
            Bi = Parent;
            ++Depth;
        }
        return Bi;
    };

    const size_t PartCount = (std::min)(Md.vertex_buffers.size(), Md.index_buffers.size());
    if (PartCount == 0) return false;

    for (size_t Part = 0; Part < PartCount; ++Part) {
        const auto& Vb = Md.vertex_buffers[Part];
        const auto& Ib = Md.index_buffers[Part];

        const auto* PosAttr  = Vb.find_attr("POSITION");
        const auto* NormAttr = Vb.find_attr("NORMAL");
        const auto* UvAttr   = Vb.find_attr("TEXCOORD");
        const auto* BiAttr   = Vb.find_attr("BLENDINDICES");
        const auto* BwAttr   = Vb.find_attr("BLENDWEIGHT");
        if (!PosAttr) continue;

        const std::vector<int>* Remap = nullptr;
        if (Part < Md.per_vb_remap.size() && !Md.per_vb_remap[Part].empty()) {
            Remap = &Md.per_vb_remap[Part];
        } else if (!Md.remapping_table.empty()) {
            Remap = &Md.remapping_table;
        }

        const std::uint32_t VertOffset = static_cast<std::uint32_t>(Out.vertices.size());

        for (std::uint32_t Vi = 0; Vi < Vb.vertex_count; ++Vi) {
            MeshVertex Mv{};

            source2::Vec3 P = source2::read_attr_vec3(Vb, Vi, *PosAttr);
            Mv.px = P.x; Mv.py = P.y; Mv.pz = P.z;

            if (NormAttr) {
                source2::Vec3 N = source2::read_attr_vec3(Vb, Vi, *NormAttr);
                Mv.nx = N.x; Mv.ny = N.y; Mv.nz = N.z;
            } else {
                Mv.nx = 0; Mv.ny = 1.0f; Mv.nz = 0;
            }

            if (UvAttr) {
                auto [U_, V_] = source2::read_attr_uv(Vb, Vi, *UvAttr);
                Mv.u = U_; Mv.v = V_;
            }

            if (BiAttr) {
                auto Raw = Remap
                    ? source2::read_attr_blend_indices(Vb, Vi, *BiAttr, Remap)
                    : source2::read_attr_blend_indices(Vb, Vi, *BiAttr);
                const int SkelMax = SkelSize > 0 ? static_cast<int>(SkelSize - 1) : 0;
                for (int K = 0; K < 4; ++K) {
                    int BiVal = static_cast<int>(Raw[K]);
                    BiVal = std::clamp(BiVal, 0, SkelMax);
                    BiVal = CollapseToStable(BiVal);
                    BiVal = std::clamp(BiVal, 0, SkelMax);
                    Mv.joints[K] = static_cast<std::uint16_t>(BiVal);
                }
            } else {
                Mv.joints[0] = Mv.joints[1] = Mv.joints[2] = Mv.joints[3] = 0;
            }

            if (BwAttr) {
                auto W = source2::read_attr_blend_weights(Vb, Vi, *BwAttr);
                Mv.weights[0] = W[0]; Mv.weights[1] = W[1];
                Mv.weights[2] = W[2]; Mv.weights[3] = W[3];
            } else {
                Mv.weights[0] = 1.0f;
                Mv.weights[1] = Mv.weights[2] = Mv.weights[3] = 0.0f;
            }

            Out.vertices.push_back(Mv);
        }

        if (Ib.index_size == 2) {
            for (size_t I = 0; I + 1 < Ib.data.size(); I += 2) {
                std::uint16_t Idx;
                std::memcpy(&Idx, Ib.data.data() + I, 2);
                Out.indices.push_back(static_cast<std::uint32_t>(Idx) + VertOffset);
            }
        } else if (Ib.index_size == 4) {
            for (size_t I = 0; I + 3 < Ib.data.size(); I += 4) {
                std::uint32_t Idx;
                std::memcpy(&Idx, Ib.data.data() + I, 4);
                Out.indices.push_back(Idx + VertOffset);
            }
        }
    }

    if (!Out.vertices.empty() && !Out.indices.empty()) {
        RecalculateNormalsFromIndices(Out.vertices, Out.indices);
        SmoothNormals(Out.vertices);
    }

    Out.valid = !Out.vertices.empty() && !Out.indices.empty();
    return Out.valid;
}

bool EnsureVpkOpen() {
    if (s_VpkDir.is_open()) return true;
    for (const auto& Path : vpk::cs2_default_vpk_paths()) {
        if (s_VpkDir.open(Path)) break;
    }
    return s_VpkDir.is_open();
}

bool LoadModel(const std::string& ModelPath, AgentMesh& Out) {
    Out = {};
    if (!EnsureVpkOpen()) return false;

    const std::string VpkPath = NormalizePath(ModelPath, ".vmdl", ".vmdl_c");
    auto Bytes = s_VpkDir.read_file(VpkPath);
    if (!Bytes) return false;

    auto MdOpt = source2::parse_vmdl_c(Bytes->data(), Bytes->size());
    if (!MdOpt) return false;

    if (!ResolveGeometryFromRefs(s_VpkDir, *MdOpt)) return false;

    source2::build_per_vb_remaps(*MdOpt);
    return BuildMesh(*MdOpt, Out);
}

bool LoadModelFromBytes(const std::vector<uint8_t>& Bytes, vpk::VPKDir& Vpk, AgentMesh& Out) {
    Out = {};
    if (Bytes.empty()) return false;

    auto MdOpt = source2::parse_vmdl_c(Bytes.data(), Bytes.size());
    if (!MdOpt) return false;

    if (!ResolveGeometryFromRefs(Vpk, *MdOpt)) return false;

    source2::build_per_vb_remaps(*MdOpt);
    return BuildMesh(*MdOpt, Out);
}

}
