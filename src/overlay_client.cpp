#include "overlay_client.hpp"
#include "renderer/gl_loader.hpp"
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <array>
#include <chrono>

// Font bitmap for rendering FPS counter
static unsigned char font_8x8[256][8];
static bool font_initialized = false;

static void init_font() {
    std::memset(font_8x8, 0, sizeof(font_8x8));
    
    auto set_char = [](char c, const std::array<unsigned char, 8>& data) {
        for (int i = 0; i < 8; ++i) {
            font_8x8[(unsigned char)c][i] = data[i];
        }
    };

    set_char(' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    set_char(':', {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00});
    set_char('.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00});
    set_char('-', {0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x00, 0x00});
    set_char('_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00});
    
    set_char('0', {0x3c, 0x66, 0x6e, 0x76, 0x66, 0x66, 0x3c, 0x00});
    set_char('1', {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00});
    set_char('2', {0x3c, 0x66, 0x06, 0x0c, 0x30, 0x60, 0x7e, 0x00});
    set_char('3', {0x3c, 0x66, 0x06, 0x1c, 0x06, 0x66, 0x3c, 0x00});
    set_char('4', {0x0c, 0x1c, 0x3c, 0x6c, 0x6c, 0x7e, 0x0c, 0x00});
    set_char('5', {0x7e, 0x60, 0x7c, 0x06, 0x06, 0x66, 0x3c, 0x00});
    set_char('6', {0x3c, 0x60, 0x7c, 0x66, 0x66, 0x66, 0x3c, 0x00});
    set_char('7', {0x7e, 0x66, 0x0c, 0x18, 0x18, 0x18, 0x18, 0x00});
    set_char('8', {0x3c, 0x66, 0x66, 0x3c, 0x66, 0x66, 0x3c, 0x00});
    set_char('9', {0x3c, 0x66, 0x66, 0x3e, 0x06, 0x0c, 0x38, 0x00});

    set_char('A', {0x18, 0x3c, 0x66, 0x7e, 0x66, 0x66, 0x66, 0x00});
    set_char('B', {0x7c, 0x66, 0x66, 0x7c, 0x66, 0x66, 0x7c, 0x00});
    set_char('C', {0x3c, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3c, 0x00});
    set_char('D', {0x78, 0x6c, 0x66, 0x66, 0x66, 0x6c, 0x78, 0x00});
    set_char('E', {0x7e, 0x60, 0x60, 0x78, 0x60, 0x60, 0x7e, 0x00});
    set_char('F', {0x7e, 0x60, 0x60, 0x78, 0x60, 0x60, 0x60, 0x00});
    set_char('G', {0x3c, 0x66, 0x60, 0x6e, 0x66, 0x66, 0x3c, 0x00});
    set_char('H', {0x66, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x66, 0x00});
    set_char('I', {0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00});
    set_char('J', {0x1e, 0x0c, 0x0c, 0x0c, 0x0c, 0xcc, 0x78, 0x00});
    set_char('K', {0x66, 0x6c, 0x78, 0x70, 0x78, 0x6c, 0x66, 0x00});
    set_char('L', {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7e, 0x00});
    set_char('M', {0x63, 0x77, 0x7f, 0x6b, 0x63, 0x63, 0x63, 0x00});
    set_char('N', {0x66, 0x76, 0x7e, 0x7e, 0x6e, 0x66, 0x66, 0x00});
    set_char('O', {0x3c, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x00});
    set_char('P', {0x7c, 0x66, 0x66, 0x7c, 0x60, 0x60, 0x60, 0x00});
    set_char('Q', {0x3c, 0x66, 0x66, 0x66, 0x6e, 0x3c, 0x0e, 0x00});
    set_char('R', {0x7c, 0x66, 0x66, 0x7c, 0x78, 0x6c, 0x66, 0x00});
    set_char('S', {0x3c, 0x66, 0x60, 0x3c, 0x06, 0x66, 0x3c, 0x00});
    set_char('T', {0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00});
    set_char('U', {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x00});
    set_char('V', {0x66, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x18, 0x00});
    set_char('W', {0x63, 0x63, 0x63, 0x6b, 0x7f, 0x77, 0x63, 0x00});
    set_char('X', {0x66, 0x66, 0x3c, 0x18, 0x3c, 0x66, 0x66, 0x00});
    set_char('Y', {0x66, 0x66, 0x66, 0x3c, 0x18, 0x18, 0x18, 0x00});
    set_char('Z', {0x7e, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x7e, 0x00});
    
    font_initialized = true;
}

static void draw_string(const std::string& str, float x, float y, float r, float g, float b, float scale = 2.0f) {
    if (!font_initialized) init_font();

    glDisable(GL_DEPTH_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    
    // Switch temporarily to fixed function state for 2D overlays
    glUseProgram(0);

    float cur_x = x;
    for (char c : str) {
        char uc = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
        const unsigned char* bitmap = font_8x8[(unsigned char)uc];
        
        glBegin(GL_QUADS);
        for (int row = 0; row < 8; ++row) {
            unsigned char row_val = bitmap[row];
            for (int col = 0; col < 8; ++col) {
                if (row_val & (0x80 >> col)) {
                    float px = cur_x + col * scale;
                    float py = y + row * scale;
                    glColor4f(r, g, b, 1.0f);
                    glVertex2f(px, py);
                    glVertex2f(px + scale, py);
                    glVertex2f(px + scale, py + scale);
                    glVertex2f(px, py + scale);
                }
            }
        }
        glEnd();
        
        cur_x += 8.0f * scale + 2.0f;
    }
    glEnable(GL_DEPTH_TEST);
}

OverlayClient::OverlayClient(int w, int h, int x, int y, bool hyprland_support) : width(w), height(h), hyprland_support(hyprland_support) {
    init_window(x, y, hyprland_support);
    init_opengl();
    set_click_through(true);
}

OverlayClient::~OverlayClient() {
    cleanup();
}

bool OverlayClient::should_close() {
    return glfwWindowShouldClose(window);
}

void OverlayClient::poll_events() {
    glfwPollEvents();
}

void OverlayClient::set_click_through(bool enabled) {
    Display* display = glfwGetX11Display();
    if (!display) {
        std::cerr << "OVERLAY_CLIENT: Failed to get X11 display for click-through." << std::endl;
        return;
    }
    Window x_win = glfwGetX11Window(window);
    if (!x_win) {
        std::cerr << "OVERLAY_CLIENT: Failed to get X11 window." << std::endl;
        return;
    }
    if (enabled) {
        XShapeCombineRectangles(display, x_win, ShapeInput, 0, 0, nullptr, 0, ShapeSet, YXBanded);
    } else {
        XShapeCombineMask(display, x_win, ShapeInput, 0, 0, None, ShapeSet);
    }
    XFlush(display);
}

void OverlayClient::init_window(int x, int y, bool hyprland_support) {
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);

    if (!glfwInit()) {
        throw std::runtime_error("OVERLAY_CLIENT: Failed to initialize GLFW");
    }

    // Configure OpenGL context: OpenGL 3.3 Compatibility Profile (lets us use shaders + legacy immediate mode text)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);

    window = glfwCreateWindow(width, height, "fc2_chams.overlay", nullptr, nullptr);
    if (!window) {
        throw std::runtime_error("OVERLAY_CLIENT: Failed to create overlay window");
    }

    glfwSetWindowPos(window, x, y);

    // Apply X11 override_redirect to bypass window managers
    Display* display = glfwGetX11Display();
    Window x_win = glfwGetX11Window(window);
    if (hyprland_support) {
        std::cout << "OVERLAY_CLIENT: Hyprland compatibility mode enabled; skipping override_redirect "
                     "(relying on input region + windowrules for click-through)." << std::endl;
    } else if (display && x_win) {
        glfwHideWindow(window);
        XSetWindowAttributes attrs;
        attrs.override_redirect = True;
        XChangeWindowAttributes(display, x_win, CWOverrideRedirect, &attrs);
        glfwShowWindow(window);
        std::cout << "OVERLAY_CLIENT: X11 override_redirect applied successfully." << std::endl;
    } else {
        std::cerr << "OVERLAY_CLIENT: Failed to apply override_redirect." << std::endl;
    }
}

void OverlayClient::init_opengl() {
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0); // Disable V-Sync for custom frame pacing

    glViewport(0, 0, width, height);
    
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void OverlayClient::begin_frame() {
    // Keep window stacked on top (throttled to 100ms)
    static auto last_raise = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_raise).count() >= 100) {
        Display* display = glfwGetX11Display();
        Window x_win = glfwGetX11Window(window);
        if (display && x_win) {
            XRaiseWindow(display, x_win);
            XFlush(display);
        }
        last_raise = now;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void OverlayClient::draw_fps(int fps) {
    glDisable(GL_DEPTH_TEST);
    
    // Switch to orthographic 2D projection
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, width, height, 0, -10.0, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    std::string fps_text = "FPS: " + std::to_string(fps);
    float text_width = fps_text.size() * 18.0f;
    float x_pos = width - text_width - 20.0f;
    float y_pos = 20.0f;

    // Draw shadow
    draw_string(fps_text, x_pos + 1.0f, y_pos + 1.0f, 0.0f, 0.0f, 0.0f, 2.0f);
    // Draw text
    draw_string(fps_text, x_pos, y_pos, 0.0f, 1.0f, 0.0f, 2.0f);

    // Restore matrices
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    
    glEnable(GL_DEPTH_TEST);
}

void OverlayClient::end_frame() {
    glfwSwapBuffers(window);
}

void OverlayClient::cleanup() {
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
}
