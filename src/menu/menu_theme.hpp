#pragma once

#include "external/imgui/imgui.h"

inline void apply_dark_theme(float scale = 1.0f)
{
    // Classic Steam style by metasprite from ImThemes
    ImGuiStyle& style = ImGui::GetStyle();

    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.6f;
    style.WindowPadding = ImVec2(8.0f * scale, 8.0f * scale);
    style.WindowRounding = 0.0f;
    style.WindowBorderSize = 1.0f * scale;
    style.WindowMinSize = ImVec2(32.0f * scale, 32.0f * scale);
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_Left;
    style.ChildRounding = 0.0f;
    style.ChildBorderSize = 1.0f * scale;
    style.PopupRounding = 0.0f;
    style.PopupBorderSize = 1.0f * scale;
    style.FramePadding = ImVec2(4.0f * scale, 3.0f * scale);
    style.FrameRounding = 0.0f;
    style.FrameBorderSize = 1.0f * scale;
    style.ItemSpacing = ImVec2(8.0f * scale, 4.0f * scale);
    style.ItemInnerSpacing = ImVec2(4.0f * scale, 4.0f * scale);
    style.CellPadding = ImVec2(4.0f * scale, 2.0f * scale);
    style.IndentSpacing = 21.0f * scale;
    style.ColumnsMinSpacing = 6.0f * scale;
    style.ScrollbarSize = 14.0f * scale;
    style.ScrollbarRounding = 0.0f;
    style.GrabMinSize = 10.0f * scale;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.TabBorderSize = 0.0f;
    style.TabMinWidthForCloseButton = 0.0f;
    style.ColorButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

    style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.49803922f, 0.49803922f, 0.49803922f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.28627452f, 0.3372549f, 0.25882354f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.28627452f, 0.3372549f, 0.25882354f, 1.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.23921569f, 0.26666668f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.5372549f, 0.5686275f, 0.50980395f, 0.5f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.13725491f, 0.15686275f, 0.10980392f, 0.52f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.23921569f, 0.26666668f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26666668f, 0.29803923f, 0.22745098f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.29803923f, 0.3372549f, 0.25882354f, 1.0f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.23921569f, 0.26666668f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.28627452f, 0.3372549f, 0.25882354f, 1.0f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.51f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.23921569f, 0.26666668f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.34901962f, 0.41960785f, 0.30980393f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.2784314f, 0.31764707f, 0.23921569f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.24705882f, 0.29803923f, 0.21960784f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.22745098f, 0.26666668f, 0.20784314f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.5882353f, 0.5372549f, 0.1764706f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.34901962f, 0.41960785f, 0.30980393f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.5372549f, 0.5686275f, 0.50980395f, 0.5f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.28627452f, 0.3372549f, 0.25882354f, 0.4f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.34901962f, 0.41960785f, 0.30980393f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.5372549f, 0.5686275f, 0.50980395f, 0.5f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.34901962f, 0.41960785f, 0.30980393f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.34901962f, 0.41960785f, 0.30980393f, 0.6f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.5372549f, 0.5686275f, 0.50980395f, 0.5f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.13725491f, 0.15686275f, 0.10980392f, 1.0f);
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.5372549f, 0.5686275f, 0.50980395f, 1.0f);
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.5882353f, 0.5372549f, 0.1764706f, 1.0f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.1882353f, 0.22745098f, 0.1764706f, 0.0f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.5372549f, 0.5686275f, 0.50980395f, 1.0f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.5882353f, 0.5372549f, 0.1764706f, 1.0f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.34901962f, 0.41960785f, 0.30980393f, 1.0f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.5372549f, 0.5686275f, 0.50980395f, 0.78f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.5882353f, 0.5372549f, 0.1764706f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.23921569f, 0.26666668f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.34901962f, 0.41960785f, 0.30980393f, 1.0f);
    style.Colors[ImGuiCol_PlotLines] = ImVec4(0.60784316f, 0.60784316f, 0.60784316f, 1.0f);
    style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.5882353f, 0.5372549f, 0.1764706f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(1.0f, 0.7764706f, 0.2784314f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.6f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882353f, 0.1882353f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.30980393f, 0.30980393f, 0.34901962f, 1.0f);
    style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.22745098f, 0.22745098f, 0.24705882f, 1.0f);
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.5882353f, 0.5372549f, 0.1764706f, 1.0f);
    style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.7294118f, 0.6666667f, 0.23921569f, 1.0f);
    style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.5882353f, 0.5372549f, 0.1764706f, 1.0f);
    style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
    style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.35f);
}
