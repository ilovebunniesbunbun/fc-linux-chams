# Build System

## Overview

CMake 3.10+ project with C++20 standard. Three vendor libraries isolated as static targets.

## Targets

| Target | Type | Sources | Description |
|--------|------|---------|-------------|
| `fc2_chams` | Executable | Main overlay application | Primary executable |
| `test_model_load` | Executable | Standalone VPK parsing test | Diagnostic model test |
| `unit_tests` | Executable | C++ unit tests | Conditional on `-DFC2_BUILD_TESTS=ON` |
| `imgui` | STATIC | Dear ImGui + GLFW/OpenGL3 backends | Suppressed warning target |
| `meshopt` | STATIC | Meshoptimizer (allocator, codecs, optimizers) | Suppressed warning target |
| `zstd` | STATIC | Zstd compression (ASM disabled) | Suppressed warning target |

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
# standard build configuration
cmake -B build -S .                      # configure
make -j$(nproc) -C build                 # compile
./build/fc2_chams                        # run overlay
./build/test_model_load                  # run model test

# build and run C++ unit tests
cmake -B build -S . -DFC2_BUILD_TESTS=ON  # configure with unit tests
make unit_tests -j$(nproc) -C build      # compile unit tests
ctest --test-dir build --output-on-failure # run tests with ctest
```

## Notes

- Default build type: `Release` if not specified
- Vendor targets are `SYSTEM PUBLIC` for include directories
- Each vendor has `PRIVATE` warning suppression flags
