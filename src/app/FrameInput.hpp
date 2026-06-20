#pragma once
#include <glm/glm.hpp>
#include "overlay/shm_reader.hpp"

struct FrameInput {
    ShmPacket packet;
    float render_view_matrix[16];
    float gl_vp[16];
    glm::vec3 cam_pos;
    float cam_pos_arr[3];
    float dt;
};
