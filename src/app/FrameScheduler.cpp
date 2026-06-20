#include "FrameScheduler.hpp"
#include <GLFW/glfw3.h>
#include <thread>
#include <algorithm>

FrameScheduler::FrameScheduler() {
    last_frame_time = glfwGetTime();
    frame_start = std::chrono::high_resolution_clock::now();
}

void FrameScheduler::begin_frame() {
    frame_start = std::chrono::high_resolution_clock::now();
    double current_frame_time = glfwGetTime();
    dt = static_cast<float>(current_frame_time - last_frame_time);
    last_frame_time = current_frame_time;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.15f) dt = 0.15f;
}

float FrameScheduler::get_delta_time() const {
    return dt;
}

void FrameScheduler::update_vsync(bool vsync, GLFWwindow* window) {
    if (window) {
        glfwSwapInterval(vsync ? 1 : 0);
    }
}

void FrameScheduler::pace_frame(int target_fps) {
    if (target_fps > 0) {
        auto frame_end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = frame_end - frame_start;
        double target_ms = 1000.0 / target_fps;
        if (elapsed.count() < target_ms) {
            double remaining = target_ms - elapsed.count();
            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(remaining));
        }
    }
}
