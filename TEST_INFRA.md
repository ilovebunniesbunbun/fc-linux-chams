# FC2 Chams E2E Testing Infrastructure

This document describes the testing architecture, methodology, and features for end-to-end (E2E) testing of the modernized `fc2-chams` client.

## Test Harness Architecture
The test suite is built on top of **pytest** and executes the compiled `fc2_chams` binary under a virtual display using **Xvfb** (`xvfb-run`).

### Mock Bridge
To test the visual overlay in a portable, reproducible environment without requiring a running Counter-Strike 2 client, the test harness implements `MockBridge` in `tests/conftest.py`. The `MockBridge`:
1. Maps to the POSIX shared memory file `/dev/shm/fc2_chams_shm_bridge`.
2. Connects to and signals the named semaphore `/fc2_chams_shm_sem` using `ctypes` calling into `libc`.
3. Implements the sequence-lock protocol (Seqlock) to guarantee thread-safe transfers of player coordinate packets.

### Headless Window Management
We run tests using:
```bash
xvfb-run --server-args="-screen 0 2560x1440x24" pytest tests/
```
This launches a virtual X11 server to support the application's GLFW window context.

---

## Features Indexed

### F1: Target-Based CMake Restructuring
- **Scope**: Isolating vendor code (imgui, meshopt, zstd) behind cleanly scoped static/interface targets instead of direct source compilation, and configuring compiler warnings (`-Wall -Wextra`).
- **E2E Verification**:
  - Statically inspects `CMakeLists.txt` to confirm that vendor libraries (imgui, meshopt, zstd) are defined as separate targets and compile flags include warning levels.

### F2: OpenGL Rendering Modernization
- **Scope**: Modernizing drawing routines by eliminating OpenGL immediate-mode operations (`glBegin`/`glEnd` and legacy matrix operations) in both ESP and overlay text drawing, moving fully to modern vertex-buffered draw calls (VBOs/VAOs) and a font texture atlas.
- **E2E Verification**:
  - Performs static checks to verify no `glBegin` or `glEnd` calls exist in the overlay source code.
  - Dynamically runs the application with active players, asserting successful shader loading and modern draw pipelines.

### F3: Non-Blocking Threading and CPU Offloading
- **Scope**: Offloading VPK model loading and map geometry parsing to a background worker thread, and using a persistent BVH raytracing worker thread with a task queue and condition variable (preventing thread recreation on every frame).
- **E2E Verification**:
  - Dynamically triggers map loading and model loading in tests, asserting that the overlay continues to process frames smoothly without blocking the render thread.
  - Verifies that the persistent BVH thread handles raytrace tasks asynchronously.

### F4: Pacing, VSync & Context-Switch Minimization
- **Scope**: Adding a `"vsync"` configuration option, limiting settings menu update frequency, minimizing `glfwMakeContextCurrent` calls, and allowing the overlay to yield/sleep if no new packets are received and no window/menu updates are pending.
- **E2E Verification**:
  - Verifies reading and writing of `vsync` in `overlay.json`.
  - Captures frame times under VSync enabled/disabled states to assert pacing.
  - Verifies CPU utilization drops to negligible levels when no new SHM packets are received and window updates are idle.
