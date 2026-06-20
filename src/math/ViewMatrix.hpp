#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <chrono>
#include <cstring>

inline glm::vec3 get_camera_position(const float* view_proj) {
    glm::mat4 vp = glm::make_mat4(view_proj);
    glm::mat4 inv = glm::inverse(vp);
    
    glm::vec4 col2 = inv[2];
    glm::vec4 col3 = inv[3];
    
    glm::vec4 cam_h = col2 * -1.0f + col3;
    if (std::abs(cam_h.w) < 1e-5f) {
        return glm::vec3(0.0f, 0.0f, 0.0f);
    }
    return glm::vec3(cam_h) / cam_h.w;
}

class ViewMatrixExtrapolator {
private:
    float prev_matrix[16]{};
    float curr_matrix[16]{};
    std::chrono::high_resolution_clock::time_point prev_time;
    std::chrono::high_resolution_clock::time_point curr_time;
    bool has_prev = false;
    bool has_curr = false;

    bool matrix_equals(const float* a, const float* b) {
        for (int i = 0; i < 16; ++i) {
            if (std::abs(a[i] - b[i]) > 1e-7f) return false;
        }
        return true;
    }

public:
    void reset() {
        has_prev = false;
        has_curr = false;
    }

    void update(const float* matrix) {
        auto now = std::chrono::high_resolution_clock::now();
        if (has_curr) {
            if (matrix_equals(matrix, curr_matrix)) {
                return;
            }
            std::memcpy(prev_matrix, curr_matrix, sizeof(float) * 16);
            prev_time = curr_time;
            has_prev = true;
        }
        std::memcpy(curr_matrix, matrix, sizeof(float) * 16);
        curr_time = now;
        has_curr = true;
    }

    void get_extrapolated_matrix(float* out_matrix) {
        auto now = std::chrono::high_resolution_clock::now();
        if (!has_curr) {
            std::memset(out_matrix, 0, sizeof(float) * 16);
            return;
        }
        if (!has_prev) {
            std::memcpy(out_matrix, curr_matrix, sizeof(float) * 16);
            return;
        }

        auto dt_update = std::chrono::duration<double, std::milli>(curr_time - prev_time).count();
        auto dt_render = std::chrono::duration<double, std::milli>(now - curr_time).count();

        if (dt_update < 1.0 || dt_update > 200.0) {
            std::memcpy(out_matrix, curr_matrix, sizeof(float) * 16);
            return;
        }

        double t = dt_render / dt_update;
        if (t > 2.0) t = 2.0;
        if (t < 0.0) t = 0.0;

        for (int i = 0; i < 16; ++i) {
            out_matrix[i] = curr_matrix[i] + static_cast<float>(t) * (curr_matrix[i] - prev_matrix[i]);
        }
    }
};
