# Original User Request

## Initial Request — 2026-06-10T11:52:41Z

Refactor, modernize, and optimize the fc2-chams C++ OpenGL player overlay to improve rendering performance, eliminate blocking loading states, establish a clean, target-based CMake structure, and add a VSync toggle configuration options.

Working directory: /home/milo/Desktop/fc2-chams-rewrite/fc2-chams
Integrity mode: development

## Requirements

### R1. Target-Based CMake Restructuring
- Restructure the CMake build system to isolate third-party vendor libraries (imgui, meshopt, zstd) behind cleanly scoped static/interface targets instead of direct source compilation, and configure warning levels (`-Wall -Wextra`).

### R2. Rendering Pipeline Modernization
- Modernize the drawing routines by eliminating all OpenGL immediate-mode operations (no `glBegin`/`glEnd` or legacy matrix operations) in both the ESP skeleton/boxes and the overlay text drawing, moving fully to modern vertex-buffered draw calls and a font texture atlas.

### R3. Non-Blocking Threading and CPU Offloading
- Offload disk-bound/heavy CPU tasks (VPK file model loading, map geometry parsing, and BVH raytracing queries) off the main rendering thread to worker threads, using synchronization primitives to pass results.

### R4. Pacing, VSync GUI Toggle, and Context-Switch Minimization
- Add a `"vsync"` boolean option to `OverlayConfig` and the JSON configuration file (`overlay.json`).
- Add a VSync toggle checkbox/button in the ImGui settings window (`MenuClient`). Toggling VSync should dynamically call `glfwSwapInterval(1)` (enabled) or `glfwSwapInterval(0)` (disabled).
- Limit settings menu rendering/update frequency (e.g., 60 FPS or on-event) and only call `glfwMakeContextCurrent` when transitioning between windows if the target context is not already active.

## Acceptance Criteria

### Performance & Overhead
- [ ] Toggling VSync on throttles the overlay to the monitor refresh rate.
- [ ] OpenGL context switches (`glfwMakeContextCurrent`) are minimized and settings menu updates are paced to prevent GPU driver stalls.
- [ ] If no new data packet has been retrieved from the shared memory bridge and there are no window/menu updates, the overlay yields/sleeps instead of spinning at 100% CPU/GPU.

### Modern OpenGL Execution
- [ ] The application compiles and runs successfully. All rendering in the visual overlay (ESP boxes, skeletons, text) uses vertex buffers (VBO/VAO) and shader programs without legacy immediate mode blocks.

### GUI & Config Integration
- [ ] The VSync setting is saved to and loaded from `overlay.json` correctly.
- [ ] The settings window (`MenuClient`) contains a working VSync toggle checkbox that immediately changes the frame pacing behavior in real-time.

### Concurrent Operations
- [ ] Map changes and first-time model loading do not freeze or hitch the overlay render loop; loading is processed asynchronously on a background thread.
- [ ] CPU BVH raytracing is calculated on a persistent worker thread using a condition variable instead of spawning raw threads every frame.

## Follow-up — 2026-06-10T19:17:12Z

Continue the refactoring of the fc2-chams C++ OpenGL overlay. Two of four milestones are already complete (CMake restructuring and VSync/Pacing). The remaining work is: (1) make blocking I/O and CPU-heavy raytracing non-blocking, and (2) eliminate all legacy OpenGL immediate-mode rendering.

Working directory: /home/milo/Desktop/fc2-chams-rewrite/fc2-chams
Integrity mode: development

### Context — Already Completed (DO NOT REDO)

The following changes are already applied and committed to the codebase. Do not modify or revert them:

1. **CMake Restructuring** — Vendor libraries (imgui, meshopt, zstd) are isolated as static targets with proper warning flags.
2. **VSync Toggle & Pacing** — `bool vsync` is in `OverlayConfig` and `overlay.json`. An ImGui checkbox in `MenuClient` dynamically toggles `glfwSwapInterval()`. Context switches are tracked and skipped when redundant. Menu rendering is throttled to ~60 FPS.

### R1. Non-Blocking Threading and CPU Offloading

In `src/main.cpp`:

**Async Map Loading (lines ~526-558):** The map change handler calls `MapParser::LoadMesh()` and `bvh.load_tri_file()` synchronously, blocking the render thread during map transitions. Refactor to:
- Use `std::async(std::launch::async)` to run both loads in a background task
- Check the future each frame with `wait_for(0s)`; when ready, upload geometry to the GPU on the main thread
- While loading, continue rendering normally with depth prepass disabled

**Persistent Raytrace Worker (lines ~581-648):** A new `std::thread(...).detach()` is spawned every frame a raytrace is needed, which is wasteful. Replace with:
- A single persistent worker thread created before the main loop
- Use `std::condition_variable` to wake it when work arrives
- Double-buffer results: worker writes to pending buffer, main thread swaps it in
- Join the thread cleanly on shutdown
- Remove the old `raytrace_in_progress` / detach pattern

### R2. OpenGL Immediate-Mode Elimination

All rendering currently uses legacy `glBegin`/`glEnd` with `glVertex` and `glColor` calls. Convert to modern OpenGL:

**ESP Drawing (`src/esp_drawing.hpp`, 456 lines):**
- `draw_outlined_rect()`: Uses `GL_LINE_LOOP` with `glBegin/glEnd` for box outlines
- `draw_health_bar()`: Uses `GL_QUADS` with `glBegin/glEnd` for health bar rectangles
- `draw_skeleton_chain()`: Uses `GL_LINES` with `glBegin/glEnd` for skeleton lines, with per-segment color changes based on visibility
- `draw_skeleton()`: Uses `glMatrixMode/glPushMatrix/glLoadMatrixf/glPopMatrix` for setting the view-projection matrix

Replace with a batched VBO/VAO renderer that:
- Accumulates vertices (position + color) into a dynamic buffer
- Flushes with a single `glDrawArrays` call per primitive type (lines, quads/triangles)
- Uses a simple vertex + fragment shader (position + color passthrough)
- Handles the projection matrix via a uniform instead of the fixed-function pipeline

**Text Rendering (`src/overlay_client.cpp`, `draw_string()` at line 71):**
- Renders an 8x8 bitmap font by emitting individual `GL_QUADS` per pixel
- Convert to a font texture atlas approach: bake the 8x8 bitmap font into a texture, render text as textured quads via VBO

### Acceptance Criteria

#### Concurrent Operations
- [ ] Map changes and first-time model loading do not freeze or hitch the overlay render loop; loading is processed asynchronously on a background thread.
- [ ] CPU BVH raytracing uses a persistent worker thread with a condition variable instead of spawning and detaching raw threads every frame.
- [ ] The application compiles and runs without crashes, deadlocks, or data races.

#### Modern OpenGL Execution
- [ ] All rendering in `esp_drawing.hpp` (boxes, health bars, skeletons) uses VBO/VAO and shader programs. No `glBegin`, `glEnd`, `glVertex`, or `glMatrixMode` calls remain in project source (excluding `src/external/`).
- [ ] Text rendering in `overlay_client.cpp` uses a texture atlas and VBO instead of per-pixel `GL_QUADS`.
- [ ] The application compiles and runs successfully with identical visual output to before.
