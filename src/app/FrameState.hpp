#pragma once
#include <vector>
#include <glm/glm.hpp>
#include "model_cache.hpp"
#include "vpk/source2.hpp"
#include "VisibilityWorker.hpp"

struct FrameState {
    struct RenderPalette {
        const CachedModel* model = nullptr;
        std::vector<source2::Mat3x4> skinning_palette;
        bool is_visible = true;
        std::vector<glm::vec3> sanitized_bones;
    };

    std::vector<RenderPalette> render_palettes;
    std::vector<int> player_ubo_slots;
    std::vector<int> sorted_player_indices;
    std::vector<float> dummy_vis;
    VischeckResult vischeck_result;
};
