#pragma once
#include <chrono>

struct GLFWwindow;

class FrameScheduler {
public:
    FrameScheduler();
    
    // Registers the frame start time and updates delta-time (dt)
    void begin_frame();

    // Returns the delta time of the last frame (clamped)
    float get_delta_time() const;

    // Handles VSync intervals dynamically
    void update_vsync(bool vsync, GLFWwindow* window);

    // Limit frame rate to target FPS
    void pace_frame(int target_fps);

private:
    double last_frame_time = 0.0;
    float dt = 0.0f;
    std::chrono::high_resolution_clock::time_point frame_start;
};
