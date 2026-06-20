#pragma once

struct GLFWwindow;
struct OverlayConfig;

void render_chams_tab(OverlayConfig& cfg);
void render_esp_tab(OverlayConfig& cfg);
void render_trajectories_tab(OverlayConfig& cfg);
void render_vpk_tab(OverlayConfig& cfg);
void render_overlay_tab(OverlayConfig& cfg, GLFWwindow* overlay_window, GLFWwindow* menu_window);
