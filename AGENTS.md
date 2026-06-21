# FC2 Chams V2

High-performance GPU-skinned player chams overlay for CS2 on Linux, rendering via Dear ImGui over an X11 transparent overlay.

## Quick Reference

- **Package Manager:** pacman (Arch Linux)
- **Build:** `cmake -B build -S . && make -j$(nproc) -C build`
- **Test (C++ - Model Load):** `cmake -B build -S . && make -j$(nproc) -C build && ./build/test_model_load`
- **Test (C++ - Unit Tests):** `cmake -B build -S . -DFC2_BUILD_TESTS=ON && make -j$(nproc) -C build && ctest --test-dir build --output-on-failure`
- **Test (E2E):** `python -m pytest tests/ -v`
- **Config File:** `overlay.json`
- **SHM Bridge:** `/fc2_chams_shm_bridge` (POSIX shared memory)

## Detailed Instructions

- [Agent Workflow](.claude/agent-workflow.md) — Multi-agent architecture, roles, handoff patterns
- [Code Style](.claude/code-style.md) — C++ conventions, naming, formatting
- [Build System](.claude/build-system.md) — CMake targets, vendor libs, flags
- [Architecture](.claude/architecture.md) — Rendering pipeline, model loading, SHM bridge
