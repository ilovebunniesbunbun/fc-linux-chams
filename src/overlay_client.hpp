#pragma once
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <string>

class OverlayClient {
public:
    OverlayClient(int width, int height, int x, int y, bool hyprland_support = false);
    ~OverlayClient();

    bool should_close();
    void poll_events();
    void begin_frame();
    void end_frame();
    void draw_fps(int fps);
    void set_click_through(bool enabled);

    GLFWwindow* get_window() { return window; }
    int get_width() const { return width; }
    int get_height() const { return height; }

private:
    int width;
    int height;
    bool hyprland_support = false;
    GLFWwindow* window = nullptr;

    void init_window(int x, int y, bool hyprland_support);
    void init_opengl();
    void cleanup();
};
