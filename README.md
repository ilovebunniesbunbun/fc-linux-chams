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

### Install Dependencies

#### Ubuntu / Debian
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libglfw3-dev libopengl-dev libx11-dev libxext-dev pkg-config
```

#### Arch Linux
```bash
sudo pacman -S base-devel cmake glfw-x11 glu libx11 libxext pkgconf
```

#### Fedora
```bash
sudo dnf groupinstall "Development Tools"
sudo dnf install cmake glfw-devel mesa-libGL-devel libX11-devel libXext-devel pkgconf-pkg-config
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
   * `fc2_chams_v2`: The main overlay and ImGui settings menu.
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

---

## Usage

1. **Start Counter-Strike 2**.
2. **Start your Lua/Data Bridge** (e.g. omega scripts) to set up the `/fc2_chams_shm_bridge` POSIX shared memory bridge.
3. **Run the overlay**:
   ```bash
   ./build/fc2_chams_v2
   ```
4. Drag, adjust, and customize chams to your liking on the settings menu.
5. Closing the setting menu automatically terminates the overlay and exits the process cleanly.
