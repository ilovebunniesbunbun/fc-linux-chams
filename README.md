# FC2 Chams V2

its a vpk parser and bvh and also has chams and visuals it works on linux its awesome! 

---

## Prerequisites & Dependencies

To compile and run `fc2-chams`, you must satisfy the following software dependencies.

> [!NOTE]
> This application only supports Arch-based Linux distributions.

### Required Tools & Libraries
* **Operating System**: Arch Linux (X11 server or Wayland with XWayland)
* **Compiler**: C++20 compliant compiler (GCC 10.0+ or Clang 11.0+)
* **Build System**: CMake 3.10 or higher
* **Graphics**: Drivers supporting OpenGL 3.3 Compatibility Profile (supporting modern shaders and immediate mode operations)

### Installing Dependencies

Run the following command to install the required packages on Arch Linux:

```bash
sudo pacman -S base-devel cmake glfw-x11 glu libx11 libxext nlohmann-json pkgconf
```

---

## Build Instructions

Because graphics drivers and system linkages vary, you must compile the binaries locally on your system:

1. Open a terminal in the root of the project directory (`fc2-chams/`).
2. Build the project using CMake:
   ```bash
   cmake -B build -S .
   make -j$(nproc) -C build
   ```
3. Once compilation finishes, the executables will be located in the `build/` directory:
   * **`fc2_chams`**: The main application including the transparent overlay and ImGui settings window.
   * **`test_model_load`**: A standalone diagnostic utility to verify VPK model parsing, LZ4 decompression, and caching.

---

## Usage

1. **Launch Counter-Strike 2**.
2. **Start your Lua/Data Bridge** (e.g., omega script) to initialize the `/fc2_chams_shm_bridge` POSIX shared memory bridge.
3. **Run the overlay**:
   ```bash
   ./build/fc2_chams
   ```
4. Customize settings (Chams styles, RGBA colors, occlusion modes) inside the ImGui control menu. Click **"Save Configuration"** to write settings to `overlay.json`.
5. Closing the settings menu will cleanly terminate both the overlay and the configuration utility.

---

## Features Tree

```
fc2-chams
├── Real-Time Visual Overlay
│   ├── Skinned 3D Player Chams
│   │   ├── Flat/Solid shader styling
│   │   ├── Metallic & Fresnel lighting shaders
│   │   ├── Glow Blend & Pulsing styles
│   │   └── Zero-allocation rendering optimizations
│   ├── Dynamic Occlusion Engine
│   │   ├── GPU Depth Prepass (Renders map geometry for pixel-perfect wall occlusion)
│   │   └── CPU BVH Raytrace Fallback (Multi-threaded BVH raycaster when GPU data is missing)
│   └── Overlay Drawings
│       ├── Precision bounding box ESP (VBO/VAO optimized)
│       ├── Skeletons & Joint connections
│       └── Custom font rendering with texture atlasing
├── Interactive Control Panel
│   ├── Separate X11 settings window using Dear ImGui
│   ├── Shared OpenGL contexts (eliminates duplication of resources)
│   ├── Save & Load configuration profiles (saved to overlay.json)
│   └── VSync control & menu refresh rate limiting
├── Asset Engine
│   ├── Multi-path VPK Reader (pak01_dir.vpk & map-specific VPKs)
│   ├── KeyValues3 (KV3) parser (supports binary/text keyvalues)
│   ├── LZ4 Decompressor (supports Version 5 block-based compression)
│   └── Non-blocking model cache (offloads assets loading to background threads)
└── Environment Integrations
    ├── Custom scaling (aspect ratio mapping: stretched, centered, offset)
    └── Hyprland window rules support
```

---

## Hyprland Configuration

When running under **Hyprland**, enable **Hyprland Compatibility Mode** in the overlay settings. Since wlroots does not natively forward input coordinates through override-redirect windows in the same way X11 does, you must define a window rule to keep the overlay floating on top, click-through, and properly aligned.

Add the following rule to your `hyprland.conf` (choose the format that matches your Hyprland version):

### Modern Hyprland syntax (`windowrulev2` - Recommended)
```hyprland
# Align and float the overlay
windowrulev2 = float, class:^(fc2_chams.overlay)$
windowrulev2 = pin, class:^(fc2_chams.overlay)$
windowrulev2 = size 100% 100%, class:^(fc2_chams.overlay)$
windowrulev2 = move 0 0, class:^(fc2_chams.overlay)$

# Make it non-interactive and cosmetic
windowrulev2 = nofocus, class:^(fc2_chams.overlay)$
windowrulev2 = noblur, class:^(fc2_chams.overlay)$
windowrulev2 = noshadow, class:^(fc2_chams.overlay)$
windowrulev2 = noanim, class:^(fc2_chams.overlay)$
windowrulev2 = borderwidth 0, class:^(fc2_chams.overlay)$
windowrulev2 = suppressevent maximize, class:^(fc2_chams.overlay)$
```

### Classic Hyprland syntax (`windowrule`)
```hyprland
windowrule {
    name = fc2chamsoverlay
    match:class = ^(fc2_chams.overlay)$
    float = on # REQUIRED — keep it out of the tiling layout
    pin = on # REQUIRED — stay on top / visible across workspaces
    no_focus = on # REQUIRED — don't steal focus from CS2 when it maps
    no_blur = true # cosmetic — no blur on the transparent surface
    no_shadow = true # cosmetic — no drop shadow
    border_size = 0 # REQUIRED-ish — no border drawn over the game
    no_anim = true # cosmetic — no open/close animation
    suppress_event = maximize  # safety — ignore maximize requests
}
```
