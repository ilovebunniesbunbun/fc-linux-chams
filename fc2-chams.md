# fc2-chams

`fc2-chams` is a high-performance, transparent player chams overlay and configuration utility designed for Linux. The application maps and visualizes player model geometries on top of Counter-Strike 2 by reading entity bone and view projection data in real-time from a POSIX shared-memory segment.

To keep gameplay completely unaffected, the visual overlay is standard-input click-through, while a separate, fully interactive configuration control panel built with **Dear ImGui** allows you to customize styles, colors, frame pacing, and occlusion modes on the fly.

---

## How It Works

1. **Shared Memory Bridge**: A separate Lua hook or memory reader inside the game writes player positions, bones, and matrices into a shared memory segment `/fc2_chams_shm_bridge`.
2. **Visual Overlay**: `fc2-chams` reads this shared memory data, scans the CS2 VPK files to load player models, and renders them in 3D using OpenGL shaders.
3. **ImGui Settings Menu**: A decorated settings window allows you to focus, interact, and customize options in real-time (e.g., changing chams styles to Metallic or Glow, modifying RGBA colors, and tweaking depth prepass).
4. **Linkage**: Closing the settings window cleanly shuts down the transparent overlay window and terminates the entire process.

---

## System Requirements

To compile and run `fc2-chams`, you must satisfy the following requirements on your system:

### Software & Compilers
* **Operating System**: Linux (X11 server environment is required; Wayland users must run it under XWayland).
* **Compiler**: A C++20 compliant compiler (GCC 10.0+ or Clang 11.0+).
* **Build Tool**: CMake 3.10 or higher.
* **OpenGL**: A graphics driver supporting OpenGL 3.3 Compatibility Profile (supporting both modern shaders and legacy immediate mode operations).

### Development Libraries
You must install development headers for GLFW3, OpenGL, X11, and the X11 Shape extensions.

* **On Debian/Ubuntu-based systems**:
  ```bash
  sudo apt-get install build-essential cmake libglfw3-dev libopengl-dev libx11-dev libxext-dev pkg-config
  ```
* **On Arch Linux**:
  ```bash
  sudo pacman -S base-devel cmake glfw-x11 glu libx11 libxext pkgconf
  ```
* **On Fedora/RedHat-based systems**:
  ```bash
  sudo dnf groupinstall "Development Tools"
  sudo dnf install cmake glfw-devel mesa-libGL-devel libX11-devel libXext-devel pkgconf-pkg-config
  ```

---

## Build Instructions

Because compiler compatibility, graphics drivers, and library linkages vary across different Linux setups, the project does **not** include precompiled binaries. You must build it locally:

1. Open a terminal in the root of the project directory.
2. Generate the build files and compile using CMake:
   ```bash
   cmake -B build -S .
   make -j$(nproc) -C build
   ```
3. Once completed, the binaries will be located inside the `build/` folder:
   * `fc2_chams`: The main overlay executable containing the chams renderer and settings window.
   * `test_model_load`: A standalone diagnostic utility to verify VPK parsing and mesh loading on your system.

---

## How to Run

1. Start Counter-Strike 2.
2. Launch your shared-memory Lua hook or writer to start broadcasting data.
3. Start the overlay application:
   ```bash
   ./build/fc2_chams
   ```
4. Customize your chams settings inside the control window. Press the "Save Configuration" button to write your active settings to `overlay.json`.
