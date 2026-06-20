# Architecture

## Overview

Three-process architecture: Game (CS2) → Lua/Data Bridge → fc2-chams overlay. Communication via POSIX shared memory at `/fc2_chams_shm_bridge`.

## Data Flow

```
CS2 → Lua Bridge → SHM Writer → [ /fc2_chams_shm_bridge ] → ShmReader → OverlayClient → GpuChamsRenderer
                                                              ↕
                                                        ModelCache ← VPKDir ← pak01_dir.vpk
                                                              ↕
                                                        DepthPrepassRenderer / BVH Raytracer
```

## Rendering Pipeline

1. **SHM Read** — `ShmReader` polls shared memory for player/entity data
2. **Model Load** — `ModelCache` parses VPK files (async, background thread)
3. **Depth Prepass** — `DepthPrepassRenderer` renders map geometry to depth buffer for occlusion
4. **BVH Fallback** — `LocalMapBVH` CPU raytracing when GPU depth data unavailable
5. **Chams Draw** — `GpuChamsRenderer` draws player models with selected shader style
6. **ESP Overlay** — `EspRenderer` draws boxes, skeletons, health bars
7. **ImGui** — `MenuClient` renders settings window (separate X11 window, shared GL context)

## Chams Styles

| Style | Description |
|-------|-------------|
| flat | Solid color |
| metallic | Fresnel-based lighting |
| glow | Glow blend with configurable thickness/intensity |
| pulse | Time-varying glow intensity |

## VSync & Pacing

- VSync toggle via ImGui → `glfwSwapInterval(0/1)`
- Settings menu capped at 60 FPS or on-event
- Context switches minimized via `glfwMakeContextCurrent` tracking
- Idle sleep when no new SHM data and no UI updates

## Config System

- JSON file: `overlay.json`
- Loaded at startup via `load_config()`, saved via `save_config()`
- Runtime modification through ImGui settings → save button

## Key Classes

| Class | File | Role |
|-------|------|------|
| `OverlayClient` | `overlay_client.hpp` | X11 window, GL context, event loop |
| `GpuChamsRenderer` | `renderer/gpu_chams.hpp` | Shader compilation, chams draw calls |
| `DepthPrepassRenderer` | `renderer/depth_prepass.hpp` | Map depth buffer rendering |
| `EspRenderer` | `renderer/esp_renderer.hpp` | 2D ESP overlay (boxes, skeletons) |
| `ModelCache` | `model_cache.hpp` | Async VPK model loading/caching |
| `MenuClient` | `menu_client.hpp` | ImGui settings window |
| `ShmReader` | `shm_reader.hpp` | Shared memory bridge reader |
| `VPKDir` | `vpk/vpk.hpp` | VPK archive directory reader |
| `LocalMapBVH` | `vischeck/bvh_parser.hpp` | CPU BVH raytracing |
