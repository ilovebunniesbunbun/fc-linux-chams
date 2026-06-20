# Build System

## Overview

CMake 3.10+ project with C++20 standard. Three vendor libraries isolated as static targets.

## Targets

| Target | Type | Sources |
|--------|------|---------|
| `fc2_chams` | Executable | Main overlay application |
| `test_model_load` | Executable | Standalone VPK parsing test |
| `imgui` | STATIC | Dear ImGui + GLFW/OpenGL3 backends |
| `meshopt` | STATIC | Meshoptimizer (allocator, codecs, optimizers) |
| `zstd` | STATIC | Zstd compression (ASM disabled) |

## Dependencies

```bash
sudo pacman -S base-devel cmake glfw-x11 glu libx11 libxext nlohmann-json pkgconf
```

## Warning Levels

- **Project code**: `-Wall -Wextra`
- **Vendor code**: `-w` (all warnings suppressed)
- Vendor headers marked `SYSTEM PUBLIC` to suppress warnings via include path

## Release Optimization

```cmake
-O3 -march=native -ffast-math -flto  # compile
-O3 -march=native -flto               # link
```

## Vendors

| Library | Source | Notes |
|---------|--------|-------|
| meshopt | `src/external/meshopt/` | 7 source files |
| zstd | `src/external/zstd/zstdimpl.cpp` | `ZSTD_DISABLE_ASM` defined |
| imgui | `src/external/imgui/` | 6 source files; links OpenGL + GLFW |

## Build Commands

```bash
cmake -B build -S .                      # configure
make -j$(nproc) -C build                 # compile
./build/fc2_chams                        # run overlay
./build/test_model_load                  # run model test
```

## Notes

- Default build type: `Release` if not specified
- Vendor targets are `SYSTEM PUBLIC` for include directories
- Each vendor has `PRIVATE` warning suppression flags
