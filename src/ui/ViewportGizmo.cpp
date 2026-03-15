#include "meshtools/ui/ViewportGizmo.h"

#include <algorithm>
#include <array>

namespace meshtools::ui {
namespace {

struct OverlayVec3 {
    float x;
    float y;
    float z;
};

OverlayVec3 rotateByCameraInverse(const OverlayVec3& vector, float yaw, float pitch) {
    const float cos_yaw = std::cos(-yaw);
    const float sin_yaw = std::sin(-yaw);
    const float cos_pitch = std::cos(pitch);
    const float sin_pitch = std::sin(pitch);

    const OverlayVec3 yaw_rotated{
        .x = (vector.x * cos_yaw) + (vector.z * sin_yaw),
        .y = vector.y,
        .z = (-vector.x * sin_yaw) + (vector.z * cos_yaw),
    };

    return OverlayVec3{
        .x = yaw_rotated.x,
        .y = (yaw_rotated.y * cos_pitch) - (yaw_rotated.z * sin_pitch),
        .z = (yaw_rotated.y * sin_pitch) + (yaw_rotated.z * cos_pitch),
    };
}

OverlayVec3 applyUpAxisTransform(const OverlayVec3& vector, UpAxis up_axis) {
    if (up_axis == UpAxis::Z) {
        return OverlayVec3{
            .x = vector.x,
            .y = vector.z,
            .z = -vector.y,
        };
    }

    return vector;
}

}  // namespace

ViewportGizmoResult drawViewportGizmo(
    const ViewportGizmoConfig& config,
    UpAxis default_up_axis,
    const mesh::MeshDocument* active_document,
    EditorUiActions* actions
) {
    constexpr float gizmo_radius = 28.0F;
    constexpr float gizmo_hit_padding = 10.0F;

    const ImVec2 gizmo_center = ImVec2(config.viewport_rect_min.x + 32.0F, config.viewport_rect_max.y - 32.0F);

    struct GizmoAxis {
        OverlayVec3 vector;
        ImU32 color;
        const char* label;
    };

    const UpAxis up_axis = active_document != nullptr ? active_document->up_axis : default_up_axis;

    const GizmoAxis axes[] = {
        GizmoAxis{.vector = applyUpAxisTransform(OverlayVec3{1.0F, 0.0F, 0.0F}, up_axis), .color = IM_COL32(231, 76, 60, 255), .label = "X"},
        GizmoAxis{.vector = applyUpAxisTransform(OverlayVec3{0.0F, 1.0F, 0.0F}, up_axis), .color = IM_COL32(46, 204, 113, 255), .label = "Y"},
        GizmoAxis{.vector = applyUpAxisTransform(OverlayVec3{0.0F, 0.0F, 1.0F}, up_axis), .color = IM_COL32(52, 152, 219, 255), .label = "Z"},
    };

    std::array<std::pair<float, GizmoAxis>, 3> sorted_axes{};
    for (std::size_t index = 0; index < sorted_axes.size(); ++index) {
        const OverlayVec3 rotated = rotateByCameraInverse(axes[index].vector, config.camera_yaw, config.camera_pitch);
        sorted_axes[index] = std::make_pair(rotated.z, GizmoAxis{
            .vector = rotated,
            .color = axes[index].color,
            .label = axes[index].label,
        });
    }
    std::sort(sorted_axes.begin(), sorted_axes.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });

    for (const auto& [depth, axis] : sorted_axes) {
        (void)depth;
        const ImVec2 endpoint = ImVec2(
            gizmo_center.x + (axis.vector.x * gizmo_radius),
            gizmo_center.y - (axis.vector.y * gizmo_radius)
        );
        config.draw_list->AddLine(gizmo_center, endpoint, axis.color, 1.25F);
        config.draw_list->AddCircleFilled(endpoint, 2.75F, axis.color, 12);
        config.draw_list->AddText(ImVec2(endpoint.x + 5.0F, endpoint.y - 6.0F), axis.color, axis.label);
    }

    const ImVec2 gizmo_hit_min = ImVec2(
        gizmo_center.x - gizmo_radius - gizmo_hit_padding,
        gizmo_center.y - gizmo_radius - gizmo_hit_padding
    );
    const ImVec2 gizmo_hit_size = ImVec2(
        (gizmo_radius * 2.0F) + (gizmo_hit_padding * 2.0F),
        (gizmo_radius * 2.0F) + (gizmo_hit_padding * 2.0F)
    );

    ImGui::SetCursorScreenPos(gizmo_hit_min);
    ImGui::InvisibleButton(
        "ViewportGizmoContextTarget",
        gizmo_hit_size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight
    );
    const bool gizmo_hovered = ImGui::IsItemHovered();
    if (ImGui::BeginPopupContextItem("ViewportGizmoContextMenu", ImGuiPopupFlags_MouseButtonRight)) {
        if (active_document == nullptr) {
            ImGui::TextDisabled("Open a project to change its up axis.");
        } else if (ImGui::MenuItem("Switch y/z up")) {
            if (actions != nullptr) {
                actions->request_set_project_up_axis =
                    active_document->up_axis == UpAxis::Y ? UpAxis::Z : UpAxis::Y;
            }
        }
        ImGui::EndPopup();
    }

    return ViewportGizmoResult{
        .hovered = gizmo_hovered,
        .context_open = ImGui::IsPopupOpen("ViewportGizmoContextMenu"),
    };
}

}  // namespace meshtools::ui
