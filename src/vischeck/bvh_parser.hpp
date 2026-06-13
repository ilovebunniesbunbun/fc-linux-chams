#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>

#include <fstream>

struct TraceResult {
    bool hit = false;
    float fraction = 1.0f;
    float distance = 99999.0f;
    Vec3 end_pos{0, 0, 0};
    Vec3 normal{0, 0, 0};
};

class LocalMapBVH {
public:
    struct AABB {
        float mins[3]{1e9f, 1e9f, 1e9f};
        float maxs[3]{-1e9f, -1e9f, -1e9f};

        void expand(const Vec3& p) {
            mins[0] = std::min(mins[0], p.x); maxs[0] = std::max(maxs[0], p.x);
            mins[1] = std::min(mins[1], p.y); maxs[1] = std::max(maxs[1], p.y);
            mins[2] = std::min(mins[2], p.z); maxs[2] = std::max(maxs[2], p.z);
        }

        bool intersects_ray(const float origin[3], const float inv_dir[3], float max_t) const {
            float t1 = (mins[0] - origin[0]) * inv_dir[0];
            float t2 = (maxs[0] - origin[0]) * inv_dir[0];
            float tmin = std::min(t1, t2);
            float tmax = std::max(t1, t2);

            for (int i = 1; i < 3; ++i) {
                float t1_ = (mins[i] - origin[i]) * inv_dir[i];
                float t2_ = (maxs[i] - origin[i]) * inv_dir[i];
                tmin = std::max(tmin, std::min(t1_, t2_));
                tmax = std::min(tmax, std::max(t1_, t2_));
            }
            return tmax >= std::max(0.0f, tmin) && tmin < max_t;
        }
    };

    struct Triangle {
        Vec3 v0, v1, v2;
    };

    struct Node {
        AABB bounds;
        int left = -1;
        int right = -1;
        int tri_start = -1;
        int tri_count = 0;
    };

    std::vector<Triangle> triangles;
    std::vector<Node> nodes;
    std::vector<int> indices;

    void build() {
        nodes.clear();
        indices.resize(triangles.size());
        for (size_t i = 0; i < triangles.size(); ++i) indices[i] = i;

        if (!triangles.empty()) {
            build_node(0, 0, triangles.size());
        }
    }

    TraceResult trace_ray(const Vec3& start, const Vec3& end) const {
        TraceResult result;
        result.end_pos = end;
        if (nodes.empty()) return result;

        float dx = end.x - start.x;
        float dy = end.y - start.y;
        float dz = end.z - start.z;
        float max_dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (max_dist < 1e-6f) return result;

        float dir[3] = { dx / max_dist, dy / max_dist, dz / max_dist };
        float origin[3] = { start.x, start.y, start.z };
        float inv_dir[3] = { 1.0f / dir[0], 1.0f / dir[1], 1.0f / dir[2] };

        float closest_t = max_dist;
        int stack[128];
        int sp = 0;
        stack[0] = 0;

        while (sp >= 0) {
            const auto& node = nodes[stack[sp--]];
            if (!node.bounds.intersects_ray(origin, inv_dir, closest_t)) continue;

            if (node.left == -1) {
                // Perform leaf intersection tests using Möller-Trumbore
                for (int i = node.tri_start; i < node.tri_start + node.tri_count; ++i) {
                    const auto& tri = triangles[indices[i]];
                    float e1x = tri.v1.x - tri.v0.x, e1y = tri.v1.y - tri.v0.y, e1z = tri.v1.z - tri.v0.z;
                    float e2x = tri.v2.x - tri.v0.x, e2y = tri.v2.y - tri.v0.y, e2z = tri.v2.z - tri.v0.z;
                    
                    float hx = dir[1] * e2z - dir[2] * e2y;
                    float hy = dir[2] * e2x - dir[0] * e2z;
                    float hz = dir[0] * e2y - dir[1] * e2x;
                    float a = e1x * hx + e1y * hy + e1z * hz;

                    if (a > -1e-6f && a < 1e-6f) continue;
                    float f = 1.0f / a;
                    float sx = origin[0] - tri.v0.x, sy = origin[1] - tri.v0.y, sz = origin[2] - tri.v0.z;
                    float u = f * (sx * hx + sy * hy + sz * hz);
                    if (u < 0.0f || u > 1.0f) continue;

                    float qx = sy * e1z - sz * e1y, qy = sz * e1x - sx * e1z, qz = sx * e1y - sy * e1x;
                    float v = f * (dir[0] * qx + dir[1] * qy + dir[2] * qz);
                    if (v < 0.0f || u + v > 1.0f) continue;

                    float t = f * (e2x * qx + e2y * qy + e2z * qz);
                    if (t > 1e-4f && t < closest_t) {
                        result.hit = true;
                        result.distance = t;
                        result.fraction = t / max_dist;
                        result.end_pos = { start.x + dir[0] * t, start.y + dir[1] * t, start.z + dir[2] * t };
                        
                        // Calculate normal vector of the triangle
                        float nx = e1y * e2z - e1z * e2y;
                        float ny = e1z * e2x - e1x * e2z;
                        float nz = e1x * e2y - e1y * e2x;
                        float nlen = std::sqrt(nx*nx + ny*ny + nz*nz);
                        if (nlen > 0.0f) {
                            result.normal = { nx / nlen, ny / nlen, nz / nlen };
                        } else {
                            result.normal = { 0, 0, 1.0f };
                        }
                        return result;
                    }
                }
            } else {
                stack[++sp] = node.right;
                stack[++sp] = node.left;
            }
        }
        return result;
    }

    // Helper to add a flat quad wall
    void add_wall(const Vec3& p1, const Vec3& p2, const Vec3& p3, const Vec3& p4) {
        // Wall split into two triangles
        triangles.push_back({p1, p2, p3});
        triangles.push_back({p1, p3, p4});
    }

    // Load mock map geometry (e.g. a simple wall barrier)
    void load_mock_geometry() {
        triangles.clear();
        // A vertical wall at Y = 1000, spanning X: -500 to 500, Z: -200 to 500
        add_wall(
            {-500.0f, 1000.0f, -200.0f},
            { 500.0f, 1000.0f, -200.0f},
            { 500.0f, 1000.0f,  500.0f},
            {-500.0f, 1000.0f,  500.0f}
        );
        build();
    }

    // Read flat binary array of 36-byte triangles
    bool load_tri_file(const std::string& path) {
        triangles.clear();
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        file.seekg(0, std::ios::end);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (size > 0 && size % 36 == 0) {
            size_t count = size / 36;
            triangles.resize(count);
            if (file.read(reinterpret_cast<char*>(triangles.data()), size)) {
                build();
                return true;
            }
        }
        triangles.clear();
        return false;
    }

private:
    void build_node(int node_idx, int start, int count) {
        if (nodes.size() <= (size_t)node_idx) {
            nodes.resize(node_idx + 1);
        }

        AABB bounds;
        for (int i = 0; i < count; ++i) {
            const auto& tri = triangles[indices[start + i]];
            bounds.expand(tri.v0);
            bounds.expand(tri.v1);
            bounds.expand(tri.v2);
        }
        nodes[node_idx].bounds = bounds;

        if (count <= 4) {
            nodes[node_idx].tri_start = start;
            nodes[node_idx].tri_count = count;
            nodes[node_idx].left = -1;
            nodes[node_idx].right = -1;
            return;
        }

        // Split node logic along longest bounding box axis
        int axis = 0;
        float dx = bounds.maxs[0] - bounds.mins[0];
        float dy = bounds.maxs[1] - bounds.mins[1];
        float dz = bounds.maxs[2] - bounds.mins[2];
        if (dy > dx) { axis = 1; dx = dy; }
        if (dz > dx) { axis = 2; }

        float split_val = (bounds.mins[axis] + bounds.maxs[axis]) * 0.5f;

        int i = start;
        int j = start + count - 1;
        while (i <= j) {
            const auto& tri = triangles[indices[i]];
            float centroid = (axis == 0) ? (tri.v0.x + tri.v1.x + tri.v2.x)/3.0f :
                             (axis == 1) ? (tri.v0.y + tri.v1.y + tri.v2.y)/3.0f :
                                           (tri.v0.z + tri.v1.z + tri.v2.z)/3.0f;
            if (centroid < split_val) {
                i++;
            } else {
                std::swap(indices[i], indices[j]);
                j--;
            }
        }

        int left_count = i - start;
        if (left_count == 0 || left_count == count) {
            left_count = count / 2;
        }

        int left_child = node_idx + 1;
        int right_child = node_idx + 2 * left_count;

        build_node(left_child, start, left_count);
        build_node(right_child, start + left_count, count - left_count);

        nodes[node_idx].left = left_child;
        nodes[node_idx].right = right_child;
    }
};
