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

void OverlayClient::init_text_rendering() {
    if (!font_initialized) init_font();

    std::vector<unsigned char> tex_data(128 * 128 * 4, 0);
    for (int c = 0; c < 256; ++c) {
        const unsigned char* bitmap = font_8x8[c];
        int char_row = c / 16;
        int char_col = c % 16;
        for (int row = 0; row < 8; ++row) {
            unsigned char row_val = bitmap[row];
            for (int col = 0; col < 8; ++col) {
                int px = char_col * 8 + col;
                int py = char_row * 8 + row;
                int idx = (py * 128 + px) * 4;
                if (row_val & (0x80 >> col)) {
                    tex_data[idx + 0] = 255;
                    tex_data[idx + 1] = 255;
                    tex_data[idx + 2] = 255;
                    tex_data[idx + 3] = 255;
                } else {
                    tex_data[idx + 0] = 255;
                    tex_data[idx + 1] = 255;
                    tex_data[idx + 2] = 255;
                    tex_data[idx + 3] = 0;
                }
            }
        }
    }

    glGenTextures(1, &font_texture);
    glBindTexture(GL_TEXTURE_2D, font_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex_data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Compile text shaders
    const char* text_vertex_src = R"glsl(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec2 aTex;
        out vec2 vTex;
        uniform mat4 uProj;
        void main() {
            gl_Position = uProj * vec4(aPos, 0.0, 1.0);
            vTex = aTex;
        }
    )glsl";

    const char* text_fragment_src = R"glsl(
        #version 330 core
        in vec2 vTex;
        out vec4 fragColor;
        uniform sampler2D uTexture;
        uniform vec4 uColor;
        void main() {
            vec4 texColor = texture(uTexture, vTex);
            fragColor = texColor * uColor;
        }
    )glsl";

    auto compile = [](unsigned int type, const char* src) -> unsigned int {
        unsigned int s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        int success;
        glGetShaderiv(s, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[512];
            glGetShaderInfoLog(s, 512, nullptr, log);
            FC2_LOG_ERROR("TEXT SHADER COMPILATION ERROR: {}", log);
        }
        return s;
    };

    unsigned int vs = compile(GL_VERTEX_SHADER, text_vertex_src);
    unsigned int fs = compile(GL_FRAGMENT_SHADER, text_fragment_src);
    text_program_id = glCreateProgram();
    glAttachShader(text_program_id, vs);
    glAttachShader(text_program_id, fs);
    glLinkProgram(text_program_id);
    glDeleteShader(vs);
    glDeleteShader(fs);

    text_loc_proj = glGetUniformLocation(text_program_id, "uProj");
    text_loc_color = glGetUniformLocation(text_program_id, "uColor");

    glGenVertexArrays(1, &text_vao);
    glGenBuffers(1, &text_vbo);

    glBindVertexArray(text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void OverlayClient::draw_string_batched(const std::string& str, float x, float y, float r, float g, float b, float scale) {
    if (str.empty()) return;

    if (text_program_id == 0) {
        init_text_rendering();
    }

    std::vector<TextVertex> vertices;
    vertices.reserve(str.size() * 6);

    float cur_x = x;
    for (char c : str) {
        char uc = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
        int char_row = (unsigned char)uc / 16;
        int char_col = (unsigned char)uc % 16;

        float u0 = (char_col * 8) / 128.0f;
        float v0 = (char_row * 8) / 128.0f;
        float u1 = ((char_col + 1) * 8) / 128.0f;
        float v1 = ((char_row + 1) * 8) / 128.0f;

        float w = 8.0f * scale;
        float h = 8.0f * scale;

        // Triangle 1
        vertices.push_back({cur_x,     y,     u0, v0});
        vertices.push_back({cur_x + w, y,     u1, v0});
        vertices.push_back({cur_x + w, y + h, u1, v1});

        // Triangle 2
        vertices.push_back({cur_x,     y,     u0, v0});
        vertices.push_back({cur_x + w, y + h, u1, v1});
        vertices.push_back({cur_x,     y + h, u0, v1});

        cur_x += w + 2.0f;
    }

    glUseProgram(text_program_id);

    float ortho[16] = {
        2.0f / width, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / height, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f
    };
    glUniformMatrix4fv(text_loc_proj, 1, GL_FALSE, ortho);
    float col_arr[4] = {r, g, b, 1.0f};
    glUniform4fv(text_loc_color, 1, col_arr);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, font_texture);
    
    glBindVertexArray(text_vao);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(TextVertex), vertices.data(), GL_STREAM_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices.size());

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
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
    
    // OpenGL function loading handled by GLEW in main
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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    std::string fps_text = "FPS: " + std::to_string(fps);
    float text_width = fps_text.size() * 18.0f;
    float x_pos = width - text_width - 20.0f;
    float y_pos = 20.0f;

    // Draw shadow
    draw_string_batched(fps_text, x_pos + 1.0f, y_pos + 1.0f, 0.0f, 0.0f, 0.0f, 2.0f);
    // Draw text (bright green)
    draw_string_batched(fps_text, x_pos, y_pos, 0.0f, 1.0f, 0.0f, 2.0f);

    glEnable(GL_DEPTH_TEST);
}

void OverlayClient::end_frame() {
    glfwSwapBuffers(window);
}

void OverlayClient::cleanup() {
    if (font_texture) {
        glDeleteTextures(1, &font_texture);
        font_texture = 0;
    }
    if (text_vao) {
        glDeleteVertexArrays(1, &text_vao);
        text_vao = 0;
    }
    if (text_vbo) {
        glDeleteBuffers(1, &text_vbo);
        text_vbo = 0;
    }
    if (text_program_id) {
        glDeleteProgram(text_program_id);
        text_program_id = 0;
    }
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
}
