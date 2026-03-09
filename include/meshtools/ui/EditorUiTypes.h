#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "imgui.h"
#include "meshtools/mesh/MeshDocument.h"

namespace meshtools::ui {

struct EditorUiState {
    mesh::MeshDocument* active_document = nullptr;
    std::span<const std::string> log_messages;
    float camera_yaw = 0.0F;
    float camera_pitch = 0.0F;
    struct SelectionSummary {
        std::size_t edge_count = 0;
        std::size_t face_count = 0;
        std::size_t point_count = 0;

        [[nodiscard]] std::size_t totalCount() const {
            return edge_count + face_count + point_count;
        }
    } selection_summary;
};

enum class PanModifier {
    CmdOrCtrl,
    Shift,
    RightClick,
};

inline const char* panModifierName(PanModifier modifier) {
    switch (modifier) {
        case PanModifier::CmdOrCtrl:
            return "Cmd/Ctrl";
        case PanModifier::Shift:
            return "Shift";
        case PanModifier::RightClick:
            return "Right-click";
    }

    return "Unknown";
}

struct SelectionFilters {
    bool edges = false;
    bool faces = true;
    bool points = false;
};

using UpAxis = mesh::UpAxis;

struct ViewportControlSettings {
    bool invert_y_movement = true;
    bool invert_zoom = false;
    PanModifier pan_modifier = PanModifier::Shift;
};

struct GraphicsQualitySettings {
    int render_resolution_percent = 100;
};

struct FileImportSettings {
    UpAxis up_axis = UpAxis::Y;
};

struct ViewportDisplaySettings {
    bool show_wireframe = true;
    bool shade_triangles = true;
    bool show_points = false;
};

struct ViewportCameraInput {
    ImVec2 orbit_delta = ImVec2(0.0F, 0.0F);
    ImVec2 pan_delta = ImVec2(0.0F, 0.0F);
    float zoom_delta = 0.0F;
    bool reset = false;
};

struct ViewportSelectionRequest {
    bool triggered = false;
    float normalized_x = 0.0F;
    float normalized_y = 0.0F;
};

struct EditorUiLogEvent {
    std::string origin;
    std::string message;
};

struct EditorUiActions {
    bool request_exit = false;
    bool request_open_document = false;
    bool request_save_project = false;
    ViewportCameraInput viewport_camera;
    ViewportSelectionRequest viewport_selection;
    std::vector<EditorUiLogEvent> event_logs;
};

}  // namespace meshtools::ui
