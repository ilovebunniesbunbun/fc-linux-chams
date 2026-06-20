#pragma once

#include <vector>
#include <string>

enum class GpuTimerSection {
    DEPTH_PREPASS,
    GLOW_SILHOUETTE,
    GLOW_POSTPROCESS,
    SKELETON_ESP,
    BODY_PASS,
    ESP_2D,
    COUNT
};

struct GpuProfilerData {
    std::string name;
    unsigned int queries[2] = {0, 0};
    double accumulated_ms = 0.0;
    bool active_this_frame[2] = {false, false};
};

class GpuProfiler {
public:
    GpuProfiler();
    ~GpuProfiler();

    void initialize();
    void begin_frame(bool enabled);
    void end_frame();

    void begin_section(GpuTimerSection section);
    void end_section(GpuTimerSection section);

    void print_and_reset(int packet_count, int frame_index, int player_count, double avg_ipc, double avg_cpu, double avg_total);

private:
    std::vector<GpuProfilerData> sections;
    int current_frame_idx = 0;
    bool is_enabled = false;
    bool initialized = false;
    unsigned int global_sample_count = 0;
};
