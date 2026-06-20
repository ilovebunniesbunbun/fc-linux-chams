#include "gpu_profiler.hpp"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "../logger.hpp"

GpuProfiler::GpuProfiler()
{
    sections.resize(static_cast<int>(GpuTimerSection::COUNT));
    sections[static_cast<int>(GpuTimerSection::DEPTH_PREPASS)].name = "Depth Prepass";
    sections[static_cast<int>(GpuTimerSection::GLOW_SILHOUETTE)].name = "Glow Silhouette";
    sections[static_cast<int>(GpuTimerSection::GLOW_POSTPROCESS)].name = "Glow PostProcess";
    sections[static_cast<int>(GpuTimerSection::SKELETON_ESP)].name = "Skeleton ESP";
    sections[static_cast<int>(GpuTimerSection::BODY_PASS)].name = "Body Pass";
    sections[static_cast<int>(GpuTimerSection::ESP_2D)].name = "2D ESP";
}

GpuProfiler::~GpuProfiler()
{
    if (initialized) {
        for (auto& section : sections) {
            glDeleteQueries(2, section.queries);
        }
    }
}

void GpuProfiler::initialize()
{
    if (initialized) return;

    for (auto& section : sections) {
        glGenQueries(2, section.queries);
        // Do a dummy query to initialize them and prevent GL_INVALID_OPERATION on first read
        glBeginQuery(GL_TIME_ELAPSED, section.queries[0]);
        glEndQuery(GL_TIME_ELAPSED);
        glBeginQuery(GL_TIME_ELAPSED, section.queries[1]);
        glEndQuery(GL_TIME_ELAPSED);
    }
    initialized = true;
}

void GpuProfiler::begin_frame(bool enabled)
{
    is_enabled = enabled;
    if (!is_enabled) return;
    if (!initialized) initialize();

    current_frame_idx = (current_frame_idx + 1) % 2;
    int read_idx = (current_frame_idx + 1) % 2;

    for (auto& section : sections) {
        GLuint64 time_ns = 0;
        if (section.active_this_frame[read_idx]) {
            glGetQueryObjectui64v(section.queries[read_idx], GL_QUERY_RESULT, &time_ns);
        }
        
        section.accumulated_ms += static_cast<double>(time_ns) / 1000000.0;
        section.active_this_frame[current_frame_idx] = false;
    }

    global_sample_count++;
}

void GpuProfiler::end_frame()
{
    // Implementation not needed, sections handle their own ends
}

void GpuProfiler::begin_section(GpuTimerSection section)
{
    if (!is_enabled || !initialized) return;
    int idx = static_cast<int>(section);
    glBeginQuery(GL_TIME_ELAPSED, sections[idx].queries[current_frame_idx]);
    sections[idx].active_this_frame[current_frame_idx] = true;
}

void GpuProfiler::end_section(GpuTimerSection section)
{
    if (!is_enabled || !initialized) return;
    glEndQuery(GL_TIME_ELAPSED);
}

void GpuProfiler::print_and_reset(int packet_count, int frame_index, int player_count, double avg_ipc, double avg_cpu, double avg_total)
{
    if (!is_enabled) return;

    FC2_LOG_INFO("Bridge active - received {} packets in last 5s (frame index: {}, players tracked: {})",
                 packet_count, frame_index, player_count);
    FC2_LOG_INFO("            Avg Timeline (frame processing latency):");
    FC2_LOG_INFO("            - IPC Read Time:   {:.3f} ms", avg_ipc);
    FC2_LOG_INFO("            - CPU Math Time:   {:.3f} ms", avg_cpu);
    
    double avg_gpu = 0.0;
    unsigned int samples = global_sample_count;

    for (int i = 0; i < static_cast<int>(GpuTimerSection::COUNT); ++i) {
        auto& sec = sections[i];
        double avg_ms = samples > 0 ? sec.accumulated_ms / samples : 0.0;
        avg_gpu += avg_ms;
    }

    FC2_LOG_INFO("            - GPU Render Time: {:.3f} ms (wall-clock)", avg_total - avg_ipc - avg_cpu);
    FC2_LOG_INFO("            - Total Pipeline:  {:.3f} ms", avg_total);
    FC2_LOG_INFO("            GPU Profile (avg over {} frames):", samples);

    for (int i = 0; i < static_cast<int>(GpuTimerSection::COUNT); ++i) {
        auto& sec = sections[i];
        double avg_ms = samples > 0 ? sec.accumulated_ms / samples : 0.0;
        FC2_LOG_INFO("              {:<18} {:.3f} ms", sec.name + ":", avg_ms);
        sec.accumulated_ms = 0.0;
    }
    
    global_sample_count = 0;
    
    FC2_LOG_INFO("              {:<18} {:.3f} ms", "Total GPU:", avg_gpu);
}
