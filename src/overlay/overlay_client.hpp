#pragma once
#include "renderer/gl_loader.hpp"
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

    // Modern text rendering resources
    unsigned int text_program_id = 0;
    unsigned int text_vao = 0;
    unsigned int text_vbo = 0;
    unsigned int font_texture = 0;
    int text_loc_proj = -1;
    int text_loc_color = -1;

    struct TextVertex {
        float x, y;
        float u, v;
    };

    void init_text_rendering();
    void draw_string_batched(const std::string& str, float x, float y, float r, float g, float b, float scale);

    void init_window(int x, int y, bool hyprland_support);
    void init_opengl();
    void cleanup();
};
