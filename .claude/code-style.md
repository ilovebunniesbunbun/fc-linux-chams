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
├── main.cpp              # Entry point, loop orchestration
├── config.{hpp,cpp}      # Config loading/saving
├── menu_client.{hpp,cpp} # ImGui settings window
├── overlay_client.{hpp,cpp} # X11 overlay window management
├── model_cache.{hpp,cpp} # VPK model loading/caching
├── shm_reader.hpp        # Shared memory bridge reader
├── esp_drawing.hpp       # ESP drawing primitives
├── renderer/             # OpenGL rendering
│   ├── gpu_chams.{hpp,cpp}
│   ├── depth_prepass.{hpp,cpp}
│   └── esp_renderer.{hpp,cpp}
├── vpk/                  # VPK/KV3 parsing
└── vischeck/             # BVH raytracing
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
