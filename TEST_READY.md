# E2E Test Suite Ready for CI/CD

The `fc2-chams` End-to-End (E2E) testing harness is fully implemented and ready to be integrated into any CI/CD automation workflow.

## Running the Tests

To run the complete test suite (Tiers 1-4) in a headless environment, execute the following command from the project root:

```bash
xvfb-run --server-args="-screen 0 1280x1024x24" pytest tests/ -v -s
```

*Note: Ensure the project binary is compiled (`cmake --build build`) before running the tests.*

---

## Dependencies

The E2E test suite requires the following software and packages to be installed on the host or CI runner:

### System Packages (Ubuntu/Debian)
- `xvfb` (X Virtual Framebuffer for headless display support)
- `libgl1-mesa-dri` (Mesa GL hardware acceleration / DRI fallback)
- `libglfw3-dev` (GLFW library for window context creation)

### Python Requirements
- `pytest >= 7.0.0`
- Python 3.8+ (with standard libraries: `ctypes`, `mmap`, `struct`, `subprocess`, `json`, `time`, `shutil`)

---

## Feature Coverage Mapping

| Feature Code | Feature Name | Test Location | Verification Strategy |
|---|---|---|---|
| **F1** | Target-Based CMake Restructuring | `tests/test_e2e_tier1.py` | Validates clean build environment and checks CMake target configuration structures. |
| **F2** | OpenGL Rendering Modernization | `tests/test_e2e_tier1.py`, `tests/test_e2e_tier3.py` | Asserts shader compilation, modern core context, VBO/VAO creation, and model cache data population. |
| **F3** | Non-Blocking Threading & Offloading | `tests/test_e2e_tier3.py`, `tests/test_e2e_tier4.py` | Simulates rapid map changes and concurrent model loading, asserting that main rendering is not blocked. |
| **F4** | Pacing & Context-Switch Minimization | `tests/test_e2e_tier3.py` | Verifies behavior under different pacing targets (FPS config targets) and overlay idle/yield cycles. |

---

## Acceptance Test Tiers

The test files are organized by acceptance tiers to match the project test requirements:

1. **Tier 1 (Feature Coverage)**: `tests/test_e2e_tier1.py`
   - Verifies process launching, configuration parsing, OpenGL modern core initialization, and shared memory communication initialization.
2. **Tier 2 (Boundary/Corner Cases)**: `tests/test_e2e_tier2.py`
   - Verifies max players (64), empty lists (0), rapid map switching, corrupted config fallback, missing resources, and malformed coordinate values (NaN/Inf).
3. **Tier 3 (Cross-feature Logic)**: `tests/test_e2e_tier3.py`
   - Verifies FPS pacing target behavior, concurrent map parsing, and background loading responsiveness.
4. **Tier 4 (Application Scenarios)**: `tests/test_e2e_tier4.py`
   - Simulates a full CS2 match scenario with multiple players, rounds, damage events, player deaths, and clean disconnects.
