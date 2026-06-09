# fc2-chams-v2: Merge Plan

## Overview

**Goal**: Merge the Linux overlay infrastructure and fc2 Lua bridge from `fc2_chams` with the superior VPK parsing, Source2 VMDL model loading, and chams rendering pipeline from `vpk-parser` to create a single, improved Linux overlay application.

---

## Project Architecture Comparison

### fc2_chams (Linux — what we KEEP)

| Component | File(s) | Description |
|---|---|---|
| **Lua ↔ C++ Bridge** | `collector_bridge.lua` + `shm_reader.hpp` | POSIX shared memory (`/fc2_chams_shm_bridge`) with seqlock protocol + named semaphore wakeup. The Lua script runs inside the fc2 engine, reads bones/view matrix/model names from CS2 process memory via `process_vm_readv`, and streams `ShmPacket` structs to the C++ overlay. |
| **SHM Packet Protocol** | `shm_reader.hpp` | `ShmPacket` = frame_index + view_matrix[16] + local_eye + map_name + 64×PlayerData (team, health, active, origin, model_name[64], bone_count, BoneTransform[128]) |
| **Linux Overlay Window** | `overlay_client.cpp/.hpp` | GLFW + OpenGL + X11 shape extension click-through. Fullscreen transparent window with compositor bypass workaround (`height -= 1`). |
| **GLB Asset Loading** | `overlay_client.cpp` (cgltf) | Loads pre-exported `.glb` files from `meshes/` and `meshes2/` directories. Parses skinned parts, builds joint-to-game-bone mappings via name heuristics. |
| **CPU Skinning + Rendering** | `overlay_client.cpp` | Per-frame CPU skinning of bind-pose vertices using bone palette from SHM. Renders triangles with `glDrawArrays` (immediate-mode style vertex submission). |
| **BVH Raytrace Vischeck** | `bvh_parser.hpp` | Loads `.tri` files (flat binary triangle arrays) for map geometry. Builds BVH tree, traces eye→bone rays using Möller–Trumbore. Async multi-threaded per-player. |
| **View Matrix Extrapolation** | `main.cpp` | Optional linear extrapolation between consecutive view matrix updates to reduce perceived latency. |
| **JSON Config** | `overlay.json` | Monitor/game resolution, scaling mode, FPS limit, colors, maps directory. |

### vpk-parser (Windows — what we PORT)

| Component | File(s) | Description |
|---|---|---|
| **VPK Directory Parser** | `vpk/vpk.hpp` | Reads Valve VPK v2 archives (`pak01_dir.vpk`). Tree parser, CRC32, archive index resolution, embedded data support. Fully self-contained header-only. |
| **Source2 Resource Parser** | `vpk/source2.hpp` | 2300-line parser for Source2 compiled resources. Handles: resource headers, VBIB vertex/index buffers, MBUF (meshoptimizer-compressed buffers), KV3 binary format (v1–v5, LZ4/ZSTD compression), VMDL skeleton extraction, inverse bind pose computation, remapping tables. |
| **KV3 Parser** | `vpk/kv3.hpp` | Full KV3 text format parser (separate from binary KV3 in source2.hpp). |
| **VMDL Model Loader** | `vpk/vmdl/model/model.cpp` | `AgentParser::LoadModel()` — opens VPK, reads `.vmdl_c`, resolves `.vmesh_c` geometry refs, extracts position/normal/UV/blend indices/weights, builds `AgentMesh` with normals recalculation and smoothing. |
| **VMDL Top-Level** | `vpk/vmdl/vmdl.cpp/.hpp` | `VmdlParser::Load()`, `ListAll()`, `ExportToGLTF/GLB()` — high-level VMDL enumeration and export. |
| **D3D11 Chams Renderer** | `vpk/vmdl/renderer/chams_renderer.cpp/.hpp` | GPU-skinned chams with HLSL vertex/pixel shaders. Multiple styles: Textured, Flat, Metallic, Wireframe, Glow Blend, CS2 Glow. Depth prepass with map geometry for pixel-perfect occlusion. MSAA 4x anti-aliasing. Bone LOD sanitization. |
| **Map Geometry Parser** | `vpk/vmdl/maps/parser/map.cpp/.hpp` | Extracts world triangles from VPK map files (`.vmap_c` / `.vmdl_c` physics hulls). Used for depth-prepass occlusion. |
| **Map Renderer** | `vpk/vmdl/maps/renderer/map_renderer.cpp/.hpp` | D3D11 minimap/overhead renderer. |
| **Glow Effect** | `vpk/vmdl/glow_effect/glow.hpp` | Post-process outline glow using stencil + blur. |
| **D3D11 Overlay** | `overlay/overlay.cpp/.hpp` | Windows DComp overlay with UIAccess elevation, DXGI swap chain, frame-latency waitable. |
| **Systems (Windows)** | `systems/memory/`, `entities/`, `bones/`, `modules/`, `offsets/` | Windows process memory reading (`ReadProcessMemory`), module enumeration, CS2 entity traversal, bone matrix reading. **NOT NEEDED** — fc2 Lua bridge replaces all of this. |
| **External Libs** | `external/meshopt/`, `zstd/`, `stbimage/`, `nanosvg/` | meshoptimizer (vertex/index decode), Zstandard (KV3 decompression), stb_image, nanosvg. |

---

## What Changes in v2

### ❌ REMOVE from fc2_chams

| Component | Reason |
|---|---|
| **Pre-exported GLB meshes** (`meshes/`, `meshes2/`) | Replaced by runtime VPK→VMDL loading directly from CS2 game files |
| **cgltf dependency** (`external/cgltf.h`, `cgltf_impl.c`) | No longer needed — models come from Source2 VMDL format |
| **GLB loading code** (`load_glb_file()`, all cgltf parsing in `overlay_client.cpp`) | Replaced by `AgentParser::LoadModel()` from vpk-parser |
| **Hardcoded bone-name heuristics** (`map_bone_name_to_game()`, `rigid_joint_for_node()`, etc.) | vpk-parser reads the real skeleton from VMDL data with proper remapping tables |
| **CPU skinning + `glDrawArrays` rendering** | Replaced by GPU-skinned OpenGL rendering (ported from D3D11 chams_renderer) |
| **Pre-baked `.tri` map geometry files** | Replaced by runtime VPK map geometry extraction |

### ✅ KEEP from fc2_chams

| Component | Notes |
|---|---|
| **`collector_bridge.lua`** | The fc2 Lua data pipeline. No changes needed. |
| **`shm_reader.hpp`** | POSIX shared memory reader. No changes needed. |
| **`ShmPacket` protocol** | The data contract between Lua and C++. No changes. |
| **GLFW + X11 overlay window** | Keep Linux window creation, click-through, compositor workarounds. |
| **`overlay.json` config** | Keep and extend with new settings (chams style, VPK path, etc.) |
| **View matrix extrapolation** | Keep as optional feature. |
| **Frame rate limiter** | Keep high-precision pacing. |
| **BVH raytrace vischeck** | Keep as **fallback** vischeck, but primary vischeck will be GPU depth-prepass. |

### 🔄 PORT from vpk-parser (Windows → Linux)

| Component | Porting Work |
|---|---|
| **`vpk/vpk.hpp`** | ✅ Already platform-independent (just needs `#ifdef _WIN32` paths for Linux Steam directories added) |
| **`vpk/source2.hpp`** | ✅ Already platform-independent (uses `<cstring>`, `<vector>`, etc.) |
| **`vpk/kv3.hpp`** | ✅ Already platform-independent |
| **`vpk/vmdl/model/model.cpp`** | ✅ Mostly platform-independent. Just remove `stdafx.hpp` → use direct includes |
| **`vpk/vmdl/vmdl.cpp`** | ✅ Platform-independent |
| **Chams renderer** (`chams_renderer.cpp`) | 🔧 **Major work**: Port from D3D11 → OpenGL. Rewrite HLSL shaders → GLSL. Replace D3D11 constant buffers → OpenGL UBOs. Replace D3D11 depth/stencil/blend states → OpenGL state calls. |
| **Map geometry parser** (`maps/parser/map.cpp`) | ✅ Mostly platform-independent |
| **External libs** | 🔧 Need to build `meshoptimizer` and `zstd` as Linux static libs |
| **Steam path discovery** (`vpk.hpp::cs2_default_vpk_paths()`) | 🔧 Add Linux Steam library folder discovery (`~/.steam/steam/`, `~/.local/share/Steam/`, `libraryfolders.vdf`) |

### 🆕 NEW in v2

| Component | Description |
|---|---|
| **OpenGL GPU Skinning Pipeline** | Port the D3D11 vertex shader bone-skinning to OpenGL with GLSL shaders. Use SSBOs or UBOs for bone matrices. |
| **Depth Prepass (OpenGL)** | Render map geometry into a depth buffer first, then use it for per-pixel occlusion testing during chams rendering. Replaces BVH CPU raycasting as primary vischeck. |
| **MSAA Render Target** | Create MSAA FBO for anti-aliased model rendering, resolve to screen. |
| **Runtime Model Cache** | On-demand VMDL loading: when a new `model_name` appears in SHM, load it from VPK, build GPU buffers, cache. |
| **Linux VPK Path Resolver** | Scan `~/.steam/steam/steamapps/libraryfolders.vdf` to find CS2 `pak01_dir.vpk` on Linux. |

---

## Proposed Directory Structure

```
fc2-chams-v2/
├── CMakeLists.txt                    # Main build (C++20, OpenGL, GLFW, X11, zstd, meshopt)
├── overlay.json                      # Runtime config
│
├── src/
│   ├── main.cpp                      # Entry point (from fc2_chams, modified)
│   ├── overlay_client.cpp/.hpp       # Linux GLFW+OpenGL overlay window (from fc2_chams, stripped of GLB/CPU skinning)
│   ├── shm_reader.hpp                # POSIX SHM reader (from fc2_chams, unchanged)
│   │
│   ├── renderer/
│   │   ├── gpu_chams.cpp/.hpp        # OpenGL GPU-skinned chams renderer (ported from chams_renderer D3D11)
│   │   ├── depth_prepass.cpp/.hpp    # OpenGL map depth prepass for occlusion
│   │   └── shaders/
│   │       ├── chams.vert            # GLSL vertex shader (bone skinning + MVP)
│   │       ├── chams.frag            # GLSL fragment shader (flat/metallic/glow styles)
│   │       ├── depth.vert            # GLSL depth-only vertex shader
│   │       └── depth.frag            # GLSL depth-only fragment shader
│   │
│   ├── vpk/                          # Source2 asset pipeline (ported from vpk-parser)
│   │   ├── vpk.hpp                   # VPK directory parser (header-only, add Linux paths)
│   │   ├── source2.hpp               # Source2 resource parser (header-only)
│   │   ├── kv3.hpp                   # KV3 text parser (header-only)
│   │   └── vmdl/
│   │       ├── model.cpp/.hpp        # AgentMesh loader (from vpk-parser, de-Windows-ified)
│   │       ├── vmdl.cpp/.hpp         # VMDL high-level API
│   │       └── maps/
│   │           └── map_parser.cpp/.hpp  # Map geometry extractor
│   │
│   ├── vischeck/
│   │   └── bvh_parser.hpp            # BVH raytrace (from fc2_chams, kept as fallback)
│   │
│   ├── model_cache.cpp/.hpp          # Runtime model loading + GPU buffer management
│   │
│   └── external/
│       ├── meshopt/                  # meshoptimizer source
│       └── zstd/                     # Zstandard source (or system lib)
│
└── lua/
    └── collector_bridge.lua          # Symlink or copy of omega script (unchanged)
```

---

## Implementation Phases

### Phase 1: Foundation (VPK + Model Loading on Linux)

1. **Copy and strip vpk-parser headers** into `src/vpk/`:
   - `vpk.hpp` — Remove `#include <windows.h>`, add Linux Steam path discovery:
     ```cpp
     // Linux: scan ~/.steam/steam/steamapps/libraryfolders.vdf
     // and ~/.local/share/Steam/steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_dir.vpk
     ```
   - `source2.hpp` — Remove `#include <windows.h>`, already uses standard C++ otherwise. Link against system zstd or bundle source.
   - `kv3.hpp` — Already portable.

2. **Port `model.cpp`**:
   - Remove `#include "stdafx.hpp"` → use direct includes.
   - `AgentParser::EnsureVpkOpen()` → use Linux VPK paths.
   - Everything else is standard C++.

3. **Port `map.cpp`** (map geometry parser):
   - Same treatment: remove Windows includes, use direct includes.
   - `MapParser::LoadMesh()` reads from VPK, already mostly portable.

4. **Build external deps**:
   - Add meshoptimizer source files to CMake.
   - Link system `libzstd-dev` or bundle zstd source.

5. **Verify**: Write a test that opens `pak01_dir.vpk`, loads a model (e.g., `characters/models/ctm_sas/ctm_sas.vmdl_c`), and prints vertex/bone counts.

### Phase 2: OpenGL GPU Chams Renderer

1. **Create GLSL shaders** porting the D3D11 HLSL:
   - **Vertex shader** (`chams.vert`):
     ```glsl
     #version 330 core
     layout(location=0) in vec3 aPos;
     layout(location=1) in vec3 aNormal;
     layout(location=2) in vec2 aUV;
     layout(location=3) in uvec4 aJoints;
     layout(location=4) in vec4 aWeights;

     uniform mat4 uViewProj;
     uniform mat3x4 uBones[128];  // or use SSBO

     out vec3 vWorldPos;
     out vec3 vNormal;
     out vec2 vUV;

     void main() {
         // GPU bone skinning
         mat3x4 skin = uBones[aJoints.x] * aWeights.x
                     + uBones[aJoints.y] * aWeights.y
                     + uBones[aJoints.z] * aWeights.z
                     + uBones[aJoints.w] * aWeights.w;
         vec4 worldPos = vec4(skin * vec4(aPos, 1.0), 1.0);
         vWorldPos = worldPos.xyz;
         vNormal = mat3(skin) * aNormal;
         vUV = aUV;
         gl_Position = uViewProj * worldPos;
     }
     ```
   - **Fragment shader** (`chams.frag`):
     ```glsl
     #version 330 core
     uniform vec4 uColor;
     uniform int uStyle;  // 1=textured, 2=flat, 3=metallic, 4=wireframe
     uniform vec3 uCamPos;

     in vec3 vWorldPos;
     in vec3 vNormal;
     out vec4 FragColor;

     void main() {
         if (uStyle == 2) {  // flat
             FragColor = uColor;
         } else if (uStyle == 3) {  // metallic
             vec3 N = normalize(vNormal);
             vec3 V = normalize(uCamPos - vWorldPos);
             float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
             FragColor = vec4(mix(uColor.rgb * 0.3, uColor.rgb, fresnel), uColor.a);
         } else {
             FragColor = uColor;
         }
     }
     ```

2. **Create `gpu_chams.cpp`**:
   - OpenGL equivalents of D3D11 state objects:
     - Depth/stencil → `glDepthFunc`, `glStencilFunc`, `glStencilOp`
     - Blend → `glBlendFunc`, `glBlendEquation`
     - Rasterizer → `glPolygonMode`, `glCullFace`
   - VAO/VBO/IBO per cached model.
   - UBO for view-projection matrix + bone palette.

3. **Create `depth_prepass.cpp`**:
   - Render map triangles into a depth-only FBO.
   - Before chams rendering, bind this depth buffer.
   - Visible fragments: depth test passes (visible style).
   - Hidden fragments: depth test fails (hidden style).

4. **Create MSAA FBO**:
   - 4x MSAA color + depth attachments.
   - Render chams into MSAA FBO.
   - `glBlitFramebuffer` resolve to screen.

### Phase 3: Integration

1. **Replace `overlay_client.cpp` model loading**:
   - Remove all cgltf/GLB code.
   - Add `ModelCache` that maps `model_name` → `{ AgentMesh, VAO, VBO, IBO }`.
   - When a new `model_name` appears in SHM packet:
     ```cpp
     if (!model_cache.has(model_name)) {
         AgentParser::AgentMesh mesh;
         if (AgentParser::LoadModel(model_name, mesh)) {
             model_cache.insert(model_name, create_gpu_buffers(mesh));
         }
     }
     ```

2. **Replace rendering pipeline**:
   - Remove CPU skinning loops.
   - Per frame:
     1. Upload bone matrices from SHM to UBO.
     2. Run depth prepass (if map geometry loaded).
     3. For each player: bind model VAO, set bone UBO, draw with visible/hidden style.

3. **Map geometry loading**:
   - When `map_name` changes in SHM:
     ```cpp
     auto map_mesh = MapParser::LoadMesh(current_map);
     if (map_mesh.Valid) {
         depth_prepass.upload_geometry(map_mesh.Triangles);
     }
     ```
   - Falls back to BVH `.tri` files if VPK map parsing fails.

4. **Config extensions** (`overlay.json`):
   ```json
   {
       "vpk_path": "auto",
       "steam_path": "auto",
       "chams_style_visible": "flat",
       "chams_style_hidden": "flat",
       "use_depth_prepass": true,
       "use_bvh_fallback": true,
       "msaa_samples": 4,
       "glow_enabled": false,
       "glow_strength": 1.0,
       "glow_thickness": 2.0
   }
   ```

### Phase 4: Polish

1. **Glow effect** — port stencil-based glow from vpk-parser to OpenGL.
2. **Bone LOD sanitization** — port `SanitizeLodBones()` to prevent exploded bones.
3. **Workshop map support** — port `find_workshop_map_vpks()` for Linux.
4. **Model variant handling** — handle `_varianta`, `_variantb` suffixes.
5. **Hot-reload config** — watch `overlay.json` for changes.

---

## Key Technical Decisions

### 1. GPU Skinning vs CPU Skinning

**Decision: GPU skinning (OpenGL UBO + vertex shader)**

fc2_chams does CPU skinning: transforms every vertex on the CPU each frame, then uploads via `glDrawArrays`. This is slow for high-poly VMDL meshes (10k–30k vertices vs 2k–5k in GLB files).

vpk-parser does GPU skinning: uploads bone matrices to a constant buffer, vertex shader does `skin = Σ(bone[joint[i]] * weight[i])`. This is significantly faster and scales to complex meshes.

### 2. Occlusion/Vischeck Method

**Decision: Dual-mode — GPU depth prepass (primary) + BVH raytrace (fallback)**

vpk-parser renders map geometry into a depth buffer, then uses depth test pass/fail to determine visible vs hidden. This is pixel-perfect and fast.

fc2_chams uses BVH CPU raytracing from `.tri` files. Keep this as a fallback for when VPK map parsing fails or for maps without VPK data (workshop maps).

### 3. Model Source

**Decision: Runtime VPK loading (no pre-exported files)**

Eliminates the need to maintain 70+ GLB files in `meshes/` directories. Models are loaded on-demand from the CS2 game files. The `model_name` string from the Lua bridge (e.g., `characters/models/ctm_sas/ctm_sas.vmdl`) maps directly to a VPK path.

### 4. OpenGL vs Vulkan

**Decision: OpenGL 3.3+ (matching fc2_chams)**

The existing overlay uses GLFW + OpenGL, which works with the X11 compositor transparency. Vulkan would require a different transparency approach (which was explored in a prior conversation and had NVIDIA/KDE issues). Stick with OpenGL for now.

---

## Dependency Changes

### Remove
- `cgltf` (GLB parser)
- Pre-exported `.glb` mesh files

### Add
- `meshoptimizer` (vertex/index buffer decoding for Source2 MBUF format)
- `zstd` (Zstandard decompression for KV3 v2+ binary data)
- `GLEW` or `glad` (OpenGL extension loader for shader/FBO/UBO support — GLFW alone doesn't load GL 3.3+ functions)

### Keep
- `GLFW3`
- `OpenGL`
- `X11` + `Xext` (shape extension)
- `nlohmann/json`
- `rt` (POSIX shared memory)
- `pthread` (async raytracing)

### CMakeLists.txt Changes
```cmake
# New dependencies
find_package(ZSTD REQUIRED)        # or bundle source
# meshoptimizer: add_subdirectory(src/external/meshopt)

# Remove cgltf
# Remove: src/external/cgltf_impl.c

# Add new sources
set(SOURCES
    src/main.cpp
    src/overlay_client.cpp
    src/shm_reader.hpp
    src/renderer/gpu_chams.cpp
    src/renderer/depth_prepass.cpp
    src/vpk/vmdl/model.cpp
    src/vpk/vmdl/vmdl.cpp
    src/vpk/vmdl/maps/map_parser.cpp
    src/model_cache.cpp
    src/vischeck/bvh_parser.hpp
)
```

---

## SHM Protocol — No Changes Needed

The `ShmPacket` struct already carries everything the new renderer needs:

- `view_matrix[16]` — used for VP matrix in GPU chams
- `local_eye` — used for camera position in metallic fresnel shader
- `players[].model_name[64]` — used to key into VPK model loading
- `players[].bones[128]` — uploaded directly as bone palette to GPU UBO
- `players[].bone_count` — determines active bone count
- `map_name[64]` — triggers VPK map geometry loading

The Lua `collector_bridge.lua` is **completely unchanged**.

---

## Risk Assessment

| Risk | Mitigation |
|---|---|
| VPK file not found on Linux | Implement robust Steam library folder scanning + config override `vpk_path` |
| meshoptimizer decode fails for some meshes | Fallback to raw VBIB path (already in source2.hpp) |
| OpenGL bone UBO size limit (128 × mat3x4 = 6KB) | Well within GL_MAX_UNIFORM_BLOCK_SIZE (typically 64KB+) |
| Model loading latency on first encounter | Load asynchronously on background thread, render placeholder until ready |
| NVIDIA compositor issues | Keep the `height -= 1` workaround from fc2_chams |
| Workshop maps missing from VPK | Fall back to BVH `.tri` files |

---

## Summary

The merge creates a superior overlay by:

1. **Eliminating manual mesh exports** — models load directly from CS2 game files at runtime
2. **GPU-skinned rendering** — 5–10× faster than CPU skinning for complex VMDL meshes
3. **Pixel-perfect occlusion** — GPU depth prepass replaces approximate BVH raycasting
4. **Multiple chams styles** — flat, metallic, wireframe, glow (ported from vpk-parser)
5. **Anti-aliased output** — MSAA 4× for smooth edges
6. **Keeping what works** — fc2 Lua bridge, POSIX SHM, Linux overlay, BVH fallback
7. **Zero changes to Lua** — `collector_bridge.lua` continues to work identically
