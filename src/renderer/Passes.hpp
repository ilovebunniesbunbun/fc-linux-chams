#pragma once
#include "app/FrameInput.hpp"
#include "app/FrameState.hpp"
#include "config.hpp"
#include "renderer/gpu_chams.hpp"
#include "renderer/depth_prepass.hpp"
#include "renderer/esp_renderer.hpp"
#include "renderer/gpu_profiler.hpp"

struct ChamsPassInputs {
    const FrameInput& input;
    const FrameState& state;
    const OverlayConfig& cfg;
    GpuChamsRenderer& chams_renderer;
    DepthPrepassRenderer& depth_prepass;
    EspRenderer& esp_renderer;
    GpuProfiler* profiler;
    int window_width;
    int window_height;
};

class ChamsPass {
public:
    static void run(const ChamsPassInputs& inputs);
};
