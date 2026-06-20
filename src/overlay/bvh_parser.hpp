#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

struct TraceResult {
    bool hit = false;
    float fraction = 1.0f;
    float distance = 99999.0f;
    glm::vec3 end_pos{0.0f, 0.0f, 0.0f};
    glm::vec3 normal{0.0f, 0.0f, 0.0f};
};

class LocalMapBVH {
public:
    struct AABB {
        glm::vec3 mins{1e9f, 1e9f, 1e9f};
        glm::vec3 maxs{-1e9f, -1e9f, -1e9f};

        void expand(const glm::vec3& p) {
            mins = glm::min(mins, p);
            maxs = glm::max(maxs, p);
        }

        bool intersects_ray(const glm::vec3& origin, const glm::vec3& inv_dir, float max_t) const {
            glm::vec3 t1 = (mins - origin) * inv_dir;
            glm::vec3 t2 = (maxs - origin) * inv_dir;
            glm::vec3 tmin_v = glm::min(t1, t2);
            glm::vec3 tmax_v = glm::max(t1, t2);

            float tmin = std::max({ tmin_v.x, tmin_v.y, tmin_v.z });
            float tmax = std::min({ tmax_v.x, tmax_v.y, tmax_v.z });

            return tmax >= std::max(0.0f, tmin) && tmin < max_t;
        }
    };

    struct Triangle {
        glm::vec3 v0, v1, v2;
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

    TraceResult trace_ray(const glm::vec3& start, const glm::vec3& end) const {
        TraceResult result;
        result.end_pos = end;
        if (nodes.empty()) return result;

        glm::vec3 delta = end - start;
        float max_dist = glm::length(delta);
        if (max_dist < 1e-6f) return result;

        glm::vec3 dir = delta / max_dist;
        glm::vec3 origin = start;
        glm::vec3 inv_dir = 1.0f / dir;

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
                    glm::vec3 e1 = tri.v1 - tri.v0;
                    glm::vec3 e2 = tri.v2 - tri.v0;
                    
                    glm::vec3 h = glm::cross(dir, e2);
                    float a = glm::dot(e1, h);

                    if (a > -1e-6f && a < 1e-6f) continue;
                    float f = 1.0f / a;
                    glm::vec3 s = origin - tri.v0;
                    float u = f * glm::dot(s, h);
                    if (u < 0.0f || u > 1.0f) continue;

                    glm::vec3 q = glm::cross(s, e1);
                    float v = f * glm::dot(dir, q);
                    if (v < 0.0f || u + v > 1.0f) continue;

                    float t = f * glm::dot(e2, q);
                    if (t > 1e-4f && t < closest_t) {
                        result.hit = true;
                        result.distance = t;
                        result.fraction = t / max_dist;
                        result.end_pos = start + dir * t;
                        
                        // Calculate normal vector of the triangle
                        glm::vec3 normal = glm::cross(e1, e2);
                        float nlen = glm::length(normal);
                        if (nlen > 0.0f) {
                            result.normal = normal / nlen;
                        } else {
                            result.normal = glm::vec3(0.0f, 0.0f, 1.0f);
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
    void add_wall(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec3& p4) {
        // Wall split into two triangles
        triangles.push_back({p1, p2, p3});
        triangles.push_back({p1, p3, p4});
    }

    // Load mock map geometry (e.g. a simple wall barrier)
    void load_mock_geometry() {
        triangles.clear();
        // A vertical wall at Y = 1000, spanning X: -500 to 500, Z: -200 to 500
        add_wall(
            glm::vec3(-500.0f, 1000.0f, -200.0f),
            glm::vec3(500.0f, 1000.0f, -200.0f),
            glm::vec3(500.0f, 1000.0f, 500.0f),
            glm::vec3(-500.0f, 1000.0f, 500.0f)
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
        glm::vec3 size = bounds.maxs - bounds.mins;
        if (size.y > size.x) { axis = 1; }
        if (size.z > size[axis]) { axis = 2; }

        float split_val = (bounds.mins[axis] + bounds.maxs[axis]) * 0.5f;

        int i = start;
        int j = start + count - 1;
        while (i <= j) {
            const auto& tri = triangles[indices[i]];
            float centroid = (tri.v0[axis] + tri.v1[axis] + tri.v2[axis]) / 3.0f;
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
