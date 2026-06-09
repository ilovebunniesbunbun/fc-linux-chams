#!/bin/bash
set -e

IMGUI_DIR="src/external/imgui"
mkdir -p "$IMGUI_DIR"

BASE_URL="https://raw.githubusercontent.com/ocornut/imgui/v1.90.8"

echo "Downloading Dear ImGui v1.90.8 source files..."

files=(
    "imgui.h"
    "imgui.cpp"
    "imgui_draw.cpp"
    "imgui_widgets.cpp"
    "imgui_tables.cpp"
    "imgui_internal.h"
    "imconfig.h"
    "imstb_rectpack.h"
    "imstb_textedit.h"
    "imstb_truetype.h"
)

for f in "${files[@]}"; do
    echo "  - Fetching $f"
    curl -sSL -o "$IMGUI_DIR/$f" "$BASE_URL/$f"
done

echo "Downloading GLFW and OpenGL3 backends..."

backend_files=(
    "imgui_impl_glfw.h"
    "imgui_impl_glfw.cpp"
    "imgui_impl_opengl3.h"
    "imgui_impl_opengl3.cpp"
    "imgui_impl_opengl3_loader.h"
)

for bf in "${backend_files[@]}"; do
    echo "  - Fetching backends/$bf"
    curl -sSL -o "$IMGUI_DIR/$bf" "$BASE_URL/backends/$bf"
done

echo "ImGui download complete!"
