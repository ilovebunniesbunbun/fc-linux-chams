# FC2 Chams V2

A high-performance, GPU-skinned player chams visualizer and transparent overlay for Linux. This application runs concurrently with a dedicated settings control panel built using **Dear ImGui**, giving you real-time tuning capabilities over all overlay and rendering parameters.

---

## Features

* **Interactive Control Panel**: A separate, standard decorated settings window where you can focus, click, and customize options without interfering with the transparent, click-through gameplay overlay.
* **GPU-Skinned Chams**: Implements flat colors, metallic/fresnel shaders, textures, and glow blend visual styles.
* **Dynamic Occlusion Engine**:
  * **GPU Depth Prepass**: Uploads and renders CS2 VPK map geometry on the GPU for pixel-perfect wall occlusion.
  * **CPU BVH Raytrace Fallback**: An async multi-threaded Bounding Volume Hierarchy (BVH) raycaster to resolve joint visibility when GPU geometry is unavailable.
* **Precision Frame Limiter**: Features a sub-millisecond pacing loop to limit target overlay FPS smoothly.
* **Configurable Window Scaling**: Supports stretched, centered, or custom offset geometries to match your CS2 aspect ratio and window alignment.

---

## Prerequisites & Dependencies

To build the project, you need a C++20 compiler, CMake, OpenGL, GLFW3, and X11/Xext development libraries. 

> [!NOTE]
> This application only supports Arch-based distributions.

### Install Dependencies

#### Arch Linux
```bash
sudo pacman -S base-devel cmake glfw-x11 glu libx11 libxext pkgconf
```

---

## Build Instructions

1. Clone this repository to your system.
2. Build the project using CMake:
   ```bash
   cmake -B build -S .
   make -j$(nproc) -C build
   ```
3. This creates two executable binaries in the `build/` directory:
   * `fc2_chams`: The main overlay and ImGui settings menu.
   * `test_model_load`: A standalone tool to verify VPK model decompression and cache loader.

---

## Configuration (`overlay.json`)

The application loads its settings from `overlay.json` at startup and can overwrite it when you click "Save Configuration" in the menu. 

Key settings include:
* `monitor_w` / `monitor_h`: Target display dimensions.
* `game_w` / `game_h` / `scaling`: Controls aspect ratio mapping (stretched, centered, or custom).
* `fps`: Frame rate cap (e.g. `144` or `0` for unlimited).
* `chams_style_visible` / `chams_style_hidden`: Choose style styles (`metallic`, `flat`, `textured`, `glow_blend`, `cs2_glow`, `disabled`).
* `use_depth_prepass`: Toggle GPU map-based occlusion.
* `use_bvh_fallback`: Toggle CPU raycast-based occlusion.
* `maps_dir`: Directory path containing compiled `.tri` collision maps (defaults to `./maps`).
* `vpk_path`: Path to Counter-Strike `pak01_dir.vpk` (defaults to `auto` to locate via Steam directory).
* `hyprland_support`: Toggle Hyprland window compatibility mode.

---

## Usage

1. **Start Counter-Strike 2**.
2. **Start your Lua/Data Bridge** (e.g. omega scripts) to set up the `/fc2_chams_shm_bridge` POSIX shared memory bridge.
3. **Run the overlay**:
   ```bash
   ./build/fc2_chams
   ```
4. Drag, adjust, and customize chams to your liking on the settings menu.
5. Closing the setting menu automatically terminates the overlay and exits the process cleanly.

---

## Hyprland Support

If you are running Hyprland, enable **Hyprland Compatibility Mode** in the settings menu. Because wlroots does not support X11 override-redirect window inputs directly, you will need to add a window rule to your Hyprland configuration file (e.g. `hyprland.conf`) to make the overlay float correctly on top of the game:

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
