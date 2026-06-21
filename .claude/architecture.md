# Architecture

## Overview

Three-process architecture: Game (CS2) → Lua/Data Bridge → fc2-chams overlay. Communication via POSIX shared memory at `/fc2_chams_shm_bridge`.

## Data Flow

```
CS2 → Lua Bridge → SHM Writer → [ /fc2_chams_shm_bridge ] → ShmReader → App → OverlayClient
                                                               ↕          ↕
                                                         ModelCache   GrenadeTracker / GrenadeHelperData
                                                               ↕          ↕
                                                         DepthPrepass  GpuChamsRenderer / EspRenderer / GrenadeRenderer
```

## Rendering Pipeline

1. **SHM Read** — `ShmReader` polls shared memory for player/entity data, active grenades, and infernos.
2. **Model Load** — `ModelCache` parses VPK files (async, background thread).
3. **Depth Prepass** — `DepthPrepassRenderer` renders map geometry to depth buffer for occlusion.
4. **BVH Fallback** — `LocalMapBVH` CPU raytracing when GPU depth data is unavailable.
5. **Chams Draw** — `GpuChamsRenderer` draws player models with selected shader style.
6. **ESP Overlay** — `EspRenderer` draws boxes, skeletons, and health bars (depth-tested/occluded or 2D screen space).
7. **Grenades & Hulls** — `GrenadeRenderer` draws grenade trajectories, warning badges, and Inferno convex hulls.
8. **Grenade Helper** — `GrenadeHelperRenderer` draws throw spots and lineup trajectories.
9. **ImGui** — `MenuClient` renders settings window tabs (Config, Visuals, ESP, Chams, Grenade Helper, Debug) with a 2D preview panel (`MenuPreview`).

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
- Idle sleep managed by `FrameScheduler` when no new SHM data and no UI updates

## Config System

- JSON file: `overlay.json`
- Loaded at startup via `load_config()`, saved via `save_config()`
- Runtime modification through ImGui settings → save button

## Key Classes

| Class | File | Role |
|-------|------|------|
| `App` | `src/app/App.hpp` | Central application state & frame orchestration |
| `FrameScheduler` | `src/app/FrameScheduler.hpp` | Pacing, frame scheduling, & sleep management |
| `OverlayClient` | `src/overlay/overlay_client.hpp` | X11 overlay window, GL context, event loop |
| `GpuChamsRenderer` | `src/renderer/gpu_chams.hpp` | Shader compilation, chams draw calls |
| `DepthPrepassRenderer` | `src/renderer/depth_prepass.hpp` | Map depth buffer rendering |
| `EspRenderer` | `src/renderer/esp_renderer.hpp` | 2D ESP overlay (boxes, skeletons, health bars) |
| `GrenadeTracker` | `src/app/GrenadeTracker.hpp` | Tracks active grenades, trajectories, warning badges, and inferno hulls |
| `GrenadeRenderer` | `src/app/GrenadeRenderer.hpp` | Renders grenade trajectories, SVG warning badges, and convex hulls |
| `GrenadeHelperData` | `src/app/GrenadeHelperData.hpp` | Loads & parses local grenade throw setups / lineups |
| `GrenadeHelperRenderer` | `src/app/GrenadeHelperRenderer.hpp` | Renders throw spots and lineup trajectories |
| `ModelCache` | `src/model_cache.hpp` | Async VPK model loading/caching |
| `MenuClient` | `src/menu/menu_client.hpp` | ImGui settings window tabs |
| `MenuPreview` | `src/menu/menu_preview.hpp` | Interactive 2D preview panel for colors & chams |
| `ShmReader` | `src/overlay/shm_reader.hpp` | Shared memory bridge reader |
| `VPKDir` | `src/vpk/vpk.hpp` | VPK archive directory reader & parser |
| `LocalMapBVH` | `src/overlay/bvh_parser.hpp` | CPU BVH construction & raytracing |
