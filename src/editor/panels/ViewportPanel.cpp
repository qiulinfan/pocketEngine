#include "editor/panels/ViewportPanel.h"
#include "engine/core/Engine.h"
#include "imgui.h"
#include "SDL2/SDL.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace EditorPanels {
namespace {

enum class TransportIcon {
    Play,
    Pause,
    Stop
};

// 
/*
compute the size to render the runtime image:
1. fit in available viewport space
2. preserve aspect ratio
*/
ImVec2 FitRuntimeImage(const ImVec2 &available_size, int texture_width, int texture_height) {
    const float safe_texture_width = static_cast<float>(std::max(1, texture_width));
    const float safe_texture_height = static_cast<float>(std::max(1, texture_height));
    const float width_scale = available_size.x / safe_texture_width;
    const float height_scale = available_size.y / safe_texture_height;
    const float image_scale = std::max(0.0f, std::min(width_scale, height_scale));
    return ImVec2(safe_texture_width * image_scale, safe_texture_height * image_scale);
}

int TextureDimensionFromPanelSpace(float value) {
    return std::max(1, static_cast<int>(std::floor(value)));
}

void DrawTransportIcon(ImDrawList *draw_list, const ImVec2 &min, const ImVec2 &max, TransportIcon icon, ImU32 icon_color) {
    const float width = max.x - min.x;
    const float height = max.y - min.y;
    const float cx = min.x + width * 0.5f;
    const float cy = min.y + height * 0.5f;
    const float base = std::min(width, height);

    switch (icon) {
    case TransportIcon::Play: {
        const float half_h = base * 0.26f;
        const float half_w = base * 0.20f;
        const ImVec2 p1(cx - half_w, cy - half_h);
        const ImVec2 p2(cx - half_w, cy + half_h);
        const ImVec2 p3(cx + half_w * 1.35f, cy);
        draw_list->AddTriangleFilled(p1, p2, p3, icon_color);
        break;
    }
    case TransportIcon::Pause: {
        const float bar_w = base * 0.12f;
        const float bar_h = base * 0.48f;
        const float gap = base * 0.12f;
        draw_list->AddRectFilled(ImVec2(cx - gap - bar_w, cy - bar_h * 0.5f),
                                 ImVec2(cx - gap, cy + bar_h * 0.5f), icon_color,
                                 2.0f);
        draw_list->AddRectFilled(ImVec2(cx + gap, cy - bar_h * 0.5f),
                                 ImVec2(cx + gap + bar_w, cy + bar_h * 0.5f),
                                 icon_color, 2.0f);
        break;
    }
    case TransportIcon::Stop: {
        const float side = base * 0.40f;
        draw_list->AddRectFilled(ImVec2(cx - side * 0.5f, cy - side * 0.5f),
                                 ImVec2(cx + side * 0.5f, cy + side * 0.5f),
                                 icon_color, 2.0f);
        break;
    }
    }
}

bool RenderTransportButton(const char *id, TransportIcon icon,
                           const ImVec4 &button_color,
                           const ImVec4 &button_hovered_color,
                           const ImVec4 &button_active_color, bool enabled,
                           const ImVec2 &size) {
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_Button, button_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);

    if (!enabled) {
        ImGui::BeginDisabled(true);
    }
    const bool clicked = ImGui::Button("##transport_icon", size);
    if (!enabled) {
        ImGui::EndDisabled();
    }

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    DrawTransportIcon(draw_list, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                      icon, IM_COL32(245, 245, 248, enabled ? 255 : 150));

    ImGui::PopStyleColor(3);
    ImGui::PopID();
    return enabled && clicked;
}

ViewportControlsResult RenderTopCenteredTransportControls( const ImVec2 &image_min, const ImVec2 &image_size, bool play_mode_active) {
    ViewportControlsResult result;
    if (image_size.x < 120.0f || image_size.y < 80.0f) {
        return result;
    }

    // Draw a compact floating transport bar at the top-center of the viewport
    constexpr float kButtonWidth = 44.0f;
    constexpr float kButtonHeight = 36.0f;
    constexpr float kButtonSpacing = 8.0f;
    constexpr float kBarPadding = 10.0f;
    constexpr float kBarTopOffset = 12.0f;

    // bar position and size
    const float bar_width = kBarPadding * 2.0f + (kButtonWidth * 3.0f) + (kButtonSpacing * 2.0f);
    const float bar_height = kBarPadding * 2.0f + kButtonHeight;
    const ImVec2 image_center(image_min.x + image_size.x * 0.5f, image_min.y);
    const ImVec2 bar_min(image_center.x - bar_width * 0.5f, image_center.y + kBarTopOffset);
    const ImVec2 bar_max(bar_min.x + bar_width, bar_min.y + bar_height);
    result.has_transport_bar = true;
    result.transport_bar_min_x = bar_min.x;
    result.transport_bar_min_y = bar_min.y;
    result.transport_bar_max_x = bar_max.x;
    result.transport_bar_max_y = bar_max.y;

    // draw bar background
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(bar_min, bar_max, IM_COL32(20, 24, 34, 190), 12.0f);
    draw_list->AddRect(bar_min, bar_max, IM_COL32(140, 150, 180, 180), 12.0f, 0, 1.5f);
    ImGui::SetCursorScreenPos(ImVec2(bar_min.x + kBarPadding, bar_min.y + kBarPadding));

    // draw buttons, and handle clicks
    const bool play_clicked = RenderTransportButton(
        "transport_play", TransportIcon::Play, ImVec4(0.18f, 0.62f, 0.30f, 1.0f),
        ImVec4(0.24f, 0.72f, 0.37f, 1.0f), ImVec4(0.15f, 0.50f, 0.25f, 1.0f),
        !play_mode_active, ImVec2(kButtonWidth, kButtonHeight));
    if (play_clicked) {
        result.play_mode_start_requested = true;
    }

    ImGui::SameLine(0.0f, kButtonSpacing);
    const bool pause_clicked = RenderTransportButton(
        "transport_pause", TransportIcon::Pause,
        ImVec4(0.74f, 0.62f, 0.14f, 1.0f),
        ImVec4(0.84f, 0.71f, 0.20f, 1.0f), ImVec4(0.64f, 0.52f, 0.11f, 1.0f),
        play_mode_active, ImVec2(kButtonWidth, kButtonHeight));
    if (pause_clicked) {
        result.play_mode_pause_toggle_requested = true;
    }

    ImGui::SameLine(0.0f, kButtonSpacing);
    const bool stop_clicked = RenderTransportButton(
        "transport_stop", TransportIcon::Stop,
        ImVec4(0.70f, 0.22f, 0.22f, 1.0f),
        ImVec4(0.80f, 0.28f, 0.28f, 1.0f), ImVec4(0.58f, 0.18f, 0.18f, 1.0f),
        play_mode_active, ImVec2(kButtonWidth, kButtonHeight));
    if (stop_clicked) {
        result.play_mode_stop_requested = true;
    }

    return result;
}

} // namespace

ViewportControlsResult RenderViewportPanel(const Engine &engine,
                                           bool play_mode_active,
                                           bool play_mode_paused) {
    ViewportControlsResult controls_result;
    // Viewport is the bridge between runtime and editor: runtime renders into
    // an offscreen SDL texture, then ImGui displays that texture in-panel.
    if (!ImGui::Begin("Viewport", nullptr,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        return controls_result;
    }

    controls_result.panel_visible = true;
    const ImVec2 available_size = ImGui::GetContentRegionAvail();
    controls_result.requested_texture_width =
        TextureDimensionFromPanelSpace(available_size.x);
    controls_result.requested_texture_height =
        TextureDimensionFromPanelSpace(available_size.y);

    SDL_Texture *runtime_texture = engine.GetRuntimeRenderTarget();
    if (runtime_texture == nullptr) {
        ImGui::TextUnformatted("Runtime viewport is not available yet.");
        ImGui::End();
        return controls_result;
    }

    const ImVec2 image_size = FitRuntimeImage(available_size, engine.GetRuntimeRenderTargetWidth(), engine.GetRuntimeRenderTargetHeight());
    const ImVec2 cursor_screen_pos = ImGui::GetCursorScreenPos();
    const ImVec2 centered_cursor(
        cursor_screen_pos.x + std::max(0.0f, (available_size.x - image_size.x) * 0.5f),
        cursor_screen_pos.y + std::max(0.0f, (available_size.y - image_size.y) * 0.5f));

    ImGui::SetCursorScreenPos(centered_cursor);
    ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)runtime_texture), image_size);

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ImVec2 image_max(centered_cursor.x + image_size.x, centered_cursor.y + image_size.y);
    draw_list->AddRect(centered_cursor, image_max, IM_COL32(90, 168, 255, 255), 4.0f, 0, 2.0f);
    if (play_mode_active) {
        draw_list->AddText(ImVec2(centered_cursor.x + 10.0f, centered_cursor.y + 10.0f),
                           IM_COL32(245, 245, 245, 235),
                           play_mode_paused ? "PAUSED" : "PLAYING");
    } 
    else {
        draw_list->AddText(ImVec2(centered_cursor.x + 10.0f, centered_cursor.y + 10.0f),
                           IM_COL32(215, 220, 230, 210), "EDIT");
    }

    controls_result.has_runtime_image = true;
    controls_result.runtime_image_min_x = centered_cursor.x;
    controls_result.runtime_image_min_y = centered_cursor.y;
    controls_result.runtime_image_max_x = image_max.x;
    controls_result.runtime_image_max_y = image_max.y;

    const ViewportControlsResult bar_controls = RenderTopCenteredTransportControls(centered_cursor, image_size, play_mode_active);
    controls_result.play_mode_start_requested |= bar_controls.play_mode_start_requested;
    controls_result.play_mode_pause_toggle_requested |= bar_controls.play_mode_pause_toggle_requested;
    controls_result.play_mode_stop_requested |= bar_controls.play_mode_stop_requested;
    controls_result.has_transport_bar = bar_controls.has_transport_bar;
    controls_result.transport_bar_min_x = bar_controls.transport_bar_min_x;
    controls_result.transport_bar_min_y = bar_controls.transport_bar_min_y;
    controls_result.transport_bar_max_x = bar_controls.transport_bar_max_x;
    controls_result.transport_bar_max_y = bar_controls.transport_bar_max_y;

    ImGui::SetCursorScreenPos(ImVec2(cursor_screen_pos.x, centered_cursor.y + image_size.y));
    ImGui::Dummy(ImVec2(available_size.x, std::max(0.0f, available_size.y - image_size.y)));
    ImGui::End();
    return controls_result;
}

} // namespace EditorPanels
