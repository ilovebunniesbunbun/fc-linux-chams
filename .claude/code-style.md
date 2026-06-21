# Code Style

## Overview

C++20 project targeting GCC 10.0+ / Clang 11.0+ on Arch Linux.

## Naming

- **Classes**: PascalCase (`OverlayClient`, `GpuChamsRenderer`)
- **Methods/Functions**: snake_case (`load_config`, `detect_gpus`)
- **Member variables**: snake_case with no prefix (`monitor_w`, `color_vis`)
- **Structs**: PascalCase (`OverlayConfig`, `GpuDevice`)
- **Enums**: PascalCase enum, snake_case values
- **Files**: snake_case with `.hpp`/`.cpp` extensions

## Formatting

- Indentation: 4 spaces
- Opening braces on same line for functions, control flow
- Pointer/reference: attached to type (`float*`, `const float&`)
- No trailing whitespace
- Include order: standard library → system headers → project headers

## Project Structure

```
src/
├── main.cpp              # Entry point
├── config.{hpp,cpp}      # Config loading/saving
├── logger.hpp            # Simple logger
├── model_cache.{hpp,cpp} # Async VPK model loading/caching
├── app/                  # Application scheduling & features
│   ├── App.{hpp,cpp}              # Central state & loop orchestration
│   ├── FrameScheduler.{hpp,cpp}   # Frametime pacing & sleep
│   ├── FrameInput.hpp             # Shared memory input structure
│   ├── FrameState.hpp             # Active frame state
│   ├── GrenadeTracker.{hpp,cpp}   # Track/predict grenade status
│   ├── GrenadeRenderer.{hpp,cpp}  # Render grenade trajectories/badges
│   ├── GrenadeHelperData.{hpp,cpp} # Load throw lineups from disk
│   ├── GrenadeHelperRenderer.{hpp,cpp} # Render throws/helpers
│   ├── SvgCache.{hpp,cpp}         # NanoSVG caching & rendering
│   └── VisibilityWorker.{hpp,cpp} # Async raycasting queries
├── menu/                 # Settings GUI (ImGui)
│   ├── menu_client.{hpp,cpp}      # Client tabs & window
│   ├── menu_preview.{hpp,cpp}     # 2D visual style preview
│   ├── menu_tabs.{hpp,cpp}        # Individual menu tab layout
│   └── menu_theme.hpp             # ImGui visual theme config
├── overlay/              # X11 transparent overlay & SHM
│   ├── overlay_client.{hpp,cpp}   # X11 transparent overlay setup
│   ├── shm_reader.hpp             # POSIX SHM bridge reader
│   ├── bvh_parser.hpp             # CPU BVH parser & raycast (was vischeck)
│   ├── esp_drawing.hpp            # ESP primitives & font atlas
│   └── trajectory_sim.hpp         # Grenade physics simulator
├── renderer/             # OpenGL render passes & shaders
│   ├── depth_prepass.{hpp,cpp}    # Occlusion depth pass
│   ├── esp_renderer.{hpp,cpp}     # 2D/3D ESP boxes & skeletons
│   ├── gpu_chams.{hpp,cpp}        # GPU skinned player chams shaders
│   ├── Passes.{hpp,cpp}           # Base pass interface
│   ├── gl_loader.hpp              # OpenGL loader setup
│   └── gpu_profiler.{hpp,cpp}     # GPU timers & profiling UI
└── vpk/                  # Valve Package File & KV3 parser
    ├── kv3.hpp                    # KeyValues3 parser (binary/text, version 5)
    ├── source2.hpp                # Source 2 formats & schemas
    ├── vpk.hpp                    # Multi-path VPK reader
    └── vmdl/                      # Valve Model parser
        ├── model.{hpp,cpp}        # VMDL bone & mesh structure
        ├── vmdl.{hpp,cpp}         # Binary VMDL container parser
        └── maps/                  # Map geometry parsing
```

## OpenGL Conventions

- No legacy immediate mode (`glBegin`/`glEnd`)
- All rendering uses VBO/VAO + shader programs
- Shared OpenGL contexts between overlay and settings windows
- VSync controlled via `glfwSwapInterval()`

## Memory & Threading

- Offload heavy CPU/disk work to background threads
- Use `std::shared_mutex` for reader-writer synchronization
- No raw `new`/`delete` — prefer RAII
- BVH raytracing uses persistent worker thread with condition variables
