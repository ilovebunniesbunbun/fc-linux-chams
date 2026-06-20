#pragma once
#include <spdlog/spdlog.h>

#define FC2_LOG_INFO(...)  spdlog::info("[FC2 Chams] " __VA_ARGS__)
#define FC2_LOG_WARN(...)  spdlog::warn("[FC2 Chams] " __VA_ARGS__)
#define FC2_LOG_ERROR(...) spdlog::error("[FC2 Chams] " __VA_ARGS__)
#define FC2_LOG_DEBUG(...) spdlog::debug("[FC2 Chams] " __VA_ARGS__)
