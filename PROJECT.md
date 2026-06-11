# Project: FC2-Chams Modernization & Optimization

## Architecture
FC2-Chams consists of:
- **Shared Memory Bridge (`shm_reader.hpp`)**: Retrieves player data packets from Counter-Strike 2.
- **Overlay Window (`OverlayClient`)**: Transparent, click-through window rendering ESP boxes, skeletons, and overlay text.
- **Settings Window (`MenuClient`)**: Standard window rendering Dear ImGui configuration options.
- **Geometry Parser (`vpk/`)**: Loads model files (VMDL) from Counter-Strike 2 VPK files and parses map geometry.
- **Chams Renderer (`renderer/gpu_chams.cpp`)**: Skinned chams on the GPU with post-processing (glow, blur).
- **Model Cache (`model_cache.cpp`)**: Manages loaded player models.
- **BVH Parser (`vischeck/bvh_parser.hpp`)**: BVH tree for map raytracing (visibility checks).

## Code Layout
- `src/`
  - `main.cpp`: Main application entry point, render loop, and threading coordination.
  - `config.cpp`, `config.hpp`: Config load/save.
  - `overlay_client.cpp`, `overlay_client.hpp`: Overlay window management and legacy text drawing.
  - `menu_client.cpp`, `menu_client.hpp`: ImGui configuration menu.
  - `esp_drawing.hpp`: Legacy ESP box and skeleton drawing logic.
  - `model_cache.cpp`, `model_cache.hpp`: Caching and loading of 3D player models.
  - `renderer/`: Depth prepass and GPU Chams rendering.
  - `vpk/`: VPK archiving and model file parsing.
  - `vischeck/`: Map BVH parsing and raytracing.
  - `external/`: Vendor libraries (imgui, meshopt, zstd).

## Milestones

### E2E Testing Track (Parallel Track)
| Milestone | Name | Scope | Dependencies | Status |
|-----------|------|-------|--------------|--------|
| E1 | E2E Infra & Tier 1 | Setup test harness, write Tier 1 (Feature Coverage) test cases | None | DONE |
| E2 | Tiers 2 & 3 | Write Tier 2 (Boundary) and Tier 3 (Cross-Feature) test cases | E1 | DONE |
| E3 | Tier 4 & Test Ready | Write Tier 4 (Real-world workload) test cases, publish `TEST_READY.md` | E2 | DONE |


### Implementation Track
| Milestone | Name | Scope | Dependencies | Status |
|-----------|------|-------|--------------|--------|
| I1 | CMake Restructuring | Isolate imgui, meshopt, and zstd behind static/interface targets; configure compiler warnings | None | PLANNED |
| I2 | Context Sharing & Pacing | Share OpenGL contexts between overlay/menu; minimize context switches; limit menu rate; add VSync toggle | I1 | PLANNED |
| I3 | Non-Blocking Loading | Offload map/model loading to background thread; persistent BVH raytracing worker thread | I2 | PLANNED |
| I4 | OpenGL Modernization | Eliminate legacy glBegin/glEnd/matrix operations; use VBOs/VAOs for ESP; font texture atlas | I3 | PLANNED |
| I5 | Final Verification | Pass all E2E test tiers; perform Tier 5 adversarial coverage hardening | I4, E3 | PLANNED |

## Interface Contracts

### 1. OpenGL Context Sharing
- The overlay window context is the primary context. The menu window context must share resources by passing the overlay window handle as the `share` parameter to `glfwCreateWindow` in `MenuClient`.
- Shaders, textures, and buffers compiled in the overlay window context must be shared and visible in the menu context, eliminating the need to duplicate `ModelCache` and `GpuChamsRenderer` instances.

### 2. Non-Blocking Model Cache Loading
- `ModelCache::get_or_load` must not block the main rendering thread.
- If a model is not cached, the cache must return a placeholder/null representation and queue a load request to a background worker thread.
- The background thread parses the model from VPK/disk and decodes it.
- Once parsed, the model data is queued back to the main thread, which performs the OpenGL buffer creation/upload (since OpenGL resource allocation must occur on a thread with an active context, or a thread with a shared context).

### 3. Persistent BVH Raytracing
- The BVH raytracer must run on a persistent worker thread containing a task queue and a condition variable.
- On each frame needing raytracing, the main thread locks a mutex, updates the task payload with the current packet and bone visibility data, and signals the condition variable.
- The worker thread awakens, traces rays against the BVH, updates the visibility state, and sets a flag when finished. It must NOT spawn new threads or `std::async` tasks on every frame.
