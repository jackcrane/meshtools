#pragma once

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

enum class SelectionFilter {
    Edges,
    Faces,
    Points,
    Advanced,
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
};

struct ViewportCameraInput {
    ImVec2 orbit_delta = ImVec2(0.0F, 0.0F);
    ImVec2 pan_delta = ImVec2(0.0F, 0.0F);
    float zoom_delta = 0.0F;
    bool reset = false;
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
    std::vector<EditorUiLogEvent> event_logs;
};

}  // namespace meshtools::ui
