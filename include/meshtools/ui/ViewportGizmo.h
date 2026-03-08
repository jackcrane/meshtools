#pragma once

#include "meshtools/ui/EditorUiTypes.h"

namespace meshtools::ui {

struct ViewportGizmoConfig {
    ImDrawList* draw_list = nullptr;
    ImVec2 viewport_rect_min = ImVec2(0.0F, 0.0F);
    ImVec2 viewport_rect_max = ImVec2(0.0F, 0.0F);
    float camera_yaw = 0.0F;
    float camera_pitch = 0.0F;
};

struct ViewportGizmoResult {
    bool hovered = false;
    bool context_open = false;
};

ViewportGizmoResult drawViewportGizmo(
    const ViewportGizmoConfig& config,
    UpAxis default_up_axis,
    mesh::MeshDocument* active_document,
    EditorUiActions* actions
);

}  // namespace meshtools::ui
