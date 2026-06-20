#include "overlay_client.hpp"
#include "renderer/gl_loader.hpp"
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#include "logger.hpp"
#include <cstring>
#include <cstdlib>
#include <array>
#include <chrono>
#include <vector>

#include "external/imgui/imgui.h"
#include "external/imgui/imgui_impl_glfw.h"
#include "external/imgui/imgui_impl_opengl3.h"

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
        FC2_LOG_ERROR("Failed to get X11 display for click-through.");
        return;
    }
    Window x_win = glfwGetX11Window(window);
    if (!x_win) {
        FC2_LOG_ERROR("Failed to get X11 window.");
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
        FC2_LOG_INFO("Hyprland compatibility mode enabled; skipping override_redirect "
                     "(relying on input region + windowrules for click-through).");
    } else if (display && x_win) {
        glfwHideWindow(window);
        XSetWindowAttributes attrs;
        attrs.override_redirect = True;
        XChangeWindowAttributes(display, x_win, CWOverrideRedirect, &attrs);
        glfwShowWindow(window);
        FC2_LOG_INFO("X11 override_redirect applied successfully.");
    } else {
        FC2_LOG_ERROR("Failed to apply override_redirect.");
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

    // Initialize ImGui context for the overlay
    IMGUI_CHECKVERSION();
    imgui_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui_context);

    ImGuiIO& io = ImGui::GetIO();
    // Load smallest pixel-7 font
    ImFont* font = io.Fonts->AddFontFromFileTTF("assets/smallest_pixel-7.ttf", 10.0f);
    if (!font) {
        font = io.Fonts->AddFontFromFileTTF("../assets/smallest_pixel-7.ttf", 10.0f);
    }
    if (!font) {
        font = io.Fonts->AddFontFromFileTTF("/home/milo/Desktop/fc2-chams-rewrite/fc2-chams/assets/smallest_pixel-7.ttf", 10.0f);
    }
    if (!font) {
        FC2_LOG_ERROR("OVERLAY_CLIENT: Failed to load smallest_pixel-7.ttf font, falling back to default.");
        io.Fonts->AddFontDefault();
    }

    ImGui_ImplGlfw_InitForOpenGL(window, false);
    ImGui_ImplOpenGL3_Init("#version 130");
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

    // Initialize ImGui frame
    ImGui::SetCurrentContext(imgui_context);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void OverlayClient::draw_fps(int fps) {
    ImGui::SetCurrentContext(imgui_context);
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    std::string fps_text = "FPS: " + std::to_string(fps);
    
    // Width estimation: 7px per char for "smallest_pixel-7" at 10px size
    float text_width = fps_text.size() * 6.0f;
    float x_pos = width - text_width - 20.0f;
    float y_pos = 20.0f;

    // Draw shadow
    draw_list->AddText(ImVec2(x_pos + 1.0f, y_pos + 1.0f), IM_COL32(0, 0, 0, 255), fps_text.c_str());
    // Draw text (bright green)
    draw_list->AddText(ImVec2(x_pos, y_pos), IM_COL32(0, 255, 0, 255), fps_text.c_str());
}

void OverlayClient::end_frame() {
    ImGui::SetCurrentContext(imgui_context);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}

void OverlayClient::cleanup() {
    if (imgui_context) {
        ImGui::SetCurrentContext(imgui_context);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(imgui_context);
        imgui_context = nullptr;
    }
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
}
