#include "meshtools/ui/ViewportPane.h"

#include <algorithm>
#include <array>

#include <GLFW/glfw3.h>

#include "meshtools/ui/EditorDockLayout.h"
#include "meshtools/ui/ViewportGizmo.h"

namespace meshtools::ui {
namespace {

constexpr ImVec2 kOverlayButtonSize = ImVec2(22.0F, 20.0F);
constexpr float kOverlayGroupGap = 8.0F;
constexpr float kOverlayPadding = 10.0F;
constexpr float kSelectionDragThreshold = 4.0F;

bool isCmdOrCtrlHeld(GLFWwindow* window, const ImGuiIO& io) {
    if (window != nullptr) {
#if defined(__APPLE__)
        return glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
               glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
#else
        return glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
               glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
#endif
    }

#if defined(__APPLE__)
    return io.KeySuper;
#else
    return io.KeyCtrl;
#endif
}

bool pointInRect(const ImVec2& point, const ImVec2& minimum, const ImVec2& maximum) {
    return point.x >= minimum.x &&
           point.x <= maximum.x &&
           point.y >= minimum.y &&
           point.y <= maximum.y;
}

ImVec2 clampToRect(const ImVec2& point, const ImVec2& minimum, const ImVec2& maximum) {
    return ImVec2(
        std::clamp(point.x, minimum.x, maximum.x),
        std::clamp(point.y, minimum.y, maximum.y)
    );
}

}  // namespace

ViewportPane::ViewportPane(GLFWwindow* window)
    : window_(window) {}

void ViewportPane::draw(
    const EditorUiState& state,
    const ViewportControlSettings& viewport_control_settings,
    const FileImportSettings& default_file_import_settings,
    ViewportDisplaySettings& viewport_display_settings,
    SelectionFilters& selection_filters,
    std::string_view wireframe_shortcut,
    std::string_view shade_triangles_shortcut,
    std::string_view show_points_shortcut,
    std::string_view edge_shortcut,
    std::string_view face_shortcut,
    std::string_view point_shortcut,
    EditorUiActions* actions,
    const std::function<void()>& on_toggle_wireframe,
    const std::function<void()>& on_toggle_shade_triangles,
    const std::function<void()>& on_toggle_show_points,
    const std::function<void()>& on_toggle_edges,
    const std::function<void()>& on_toggle_faces,
    const std::function<void()>& on_toggle_points
) {
    constexpr ImGuiWindowFlags pane_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::Begin(kEditorViewportWindowName, nullptr, pane_flags);
    ImGui::PopStyleVar(2);
    render_size_ = ImGui::GetContentRegionAvail();
    render_size_.x = std::max(render_size_.x, 1.0F);
    render_size_.y = std::max(render_size_.y, 1.0F);

    if (window_ != nullptr) {
        int window_width = 0;
        int window_height = 0;
        int framebuffer_width = 0;
        int framebuffer_height = 0;
        glfwGetWindowSize(window_, &window_width, &window_height);
        glfwGetFramebufferSize(window_, &framebuffer_width, &framebuffer_height);

        const float scale_x = window_width > 0 ? static_cast<float>(framebuffer_width) / static_cast<float>(window_width) : 1.0F;
        const float scale_y = window_height > 0 ? static_cast<float>(framebuffer_height) / static_cast<float>(window_height) : 1.0F;
        framebuffer_scale_ = ImVec2(std::max(scale_x, 1.0F), std::max(scale_y, 1.0F));
    } else {
        framebuffer_scale_ = ImVec2(1.0F, 1.0F);
    }

    if (texture_id_ != 0) {
        ImGui::Image(
            static_cast<ImTextureID>(texture_id_),
            render_size_,
            ImVec2(0.0F, 1.0F),
            ImVec2(1.0F, 0.0F)
        );
    }
    const ImVec2 viewport_rect_min = ImGui::GetItemRectMin();
    const ImVec2 viewport_rect_max = ImGui::GetItemRectMax();
    ImGui::SetCursorScreenPos(viewport_rect_min);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton(
        "viewport_input",
        render_size_,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight
    );
    const bool viewport_interaction_hovered = ImGui::IsItemHovered();
    const bool viewport_left_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const ImVec2 controls_top_right = ImVec2(
        viewport_rect_max.x - kOverlayPadding,
        viewport_rect_min.y + kOverlayPadding
    );

    const std::array<SegmentedControlItem, 3> display_items = {{
        SegmentedControlItem{
            .label = "\xE2\x97\x87",
            .tooltip = "Show wireframe",
            .shortcut = wireframe_shortcut,
            .selected = viewport_display_settings.show_wireframe
        },
        SegmentedControlItem{
            .label = "\xE2\x97\x86",
            .tooltip = "Shade tris",
            .shortcut = shade_triangles_shortcut,
            .selected = viewport_display_settings.shade_triangles
        },
        SegmentedControlItem{
            .label = "\xE2\xA0\xAA\xE2\xA0\x82",
            .tooltip = "Show points",
            .shortcut = show_points_shortcut,
            .selected = viewport_display_settings.show_points
        },
    }};
    const int clicked_display_item = drawSegmentedControl("viewport_display", controls_top_right, display_items);
    if (clicked_display_item == 0) {
        on_toggle_wireframe();
    } else if (clicked_display_item == 1) {
        on_toggle_shade_triangles();
    } else if (clicked_display_item == 2) {
        on_toggle_show_points();
    }

    const ImVec2 filter_top_right = ImVec2(
        controls_top_right.x,
        controls_top_right.y + (static_cast<float>(display_items.size()) * kOverlayButtonSize.y) + kOverlayGroupGap
    );
    const std::array<SegmentedControlItem, 3> filter_items = {{
        SegmentedControlItem{
            .label = "\xE2\x96\xB3",
            .tooltip = "Edge selection",
            .shortcut = edge_shortcut,
            .selected = selection_filters.edges
        },
        SegmentedControlItem{
            .label = "\xE2\x96\xB2",
            .tooltip = "Face selection",
            .shortcut = face_shortcut,
            .selected = selection_filters.faces
        },
        SegmentedControlItem{
            .label = "\xE2\xA0\x95",
            .tooltip = "Point selection",
            .shortcut = point_shortcut,
            .selected = selection_filters.points
        },
    }};
    const int clicked_filter_item = drawSegmentedControl("viewport_filter", filter_top_right, filter_items);
    if (clicked_filter_item == 0) {
        on_toggle_edges();
    } else if (clicked_filter_item == 1) {
        on_toggle_faces();
    } else if (clicked_filter_item == 2) {
        on_toggle_points();
    }

    const ImVec2 display_controls_min = ImVec2(
        controls_top_right.x - kOverlayButtonSize.x,
        controls_top_right.y
    );
    const ImVec2 display_controls_max = ImVec2(
        controls_top_right.x,
        controls_top_right.y + (static_cast<float>(display_items.size()) * kOverlayButtonSize.y)
    );
    const ImVec2 filter_controls_min = ImVec2(
        filter_top_right.x - kOverlayButtonSize.x,
        filter_top_right.y
    );
    const ImVec2 filter_controls_max = ImVec2(
        filter_top_right.x,
        filter_top_right.y + (static_cast<float>(filter_items.size()) * kOverlayButtonSize.y)
    );

    const ViewportGizmoResult gizmo_result = drawViewportGizmo(
        ViewportGizmoConfig{
            .draw_list = draw_list,
            .viewport_rect_min = viewport_rect_min,
            .viewport_rect_max = viewport_rect_max,
            .camera_yaw = state.camera_yaw,
            .camera_pitch = state.camera_pitch,
        },
        default_file_import_settings.up_axis,
        state.active_document,
        actions
    );

    if (actions != nullptr && drag_selection_.active) {
        ImGuiIO& io = ImGui::GetIO();
        drag_selection_.current = clampToRect(io.MousePos, viewport_rect_min, viewport_rect_max);

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const ImVec2 drag_delta = ImVec2(
                drag_selection_.current.x - drag_selection_.start.x,
                drag_selection_.current.y - drag_selection_.start.y
            );
            const float drag_distance_squared = (drag_delta.x * drag_delta.x) + (drag_delta.y * drag_delta.y);
            if (drag_distance_squared >= (kSelectionDragThreshold * kSelectionDragThreshold)) {
                actions->viewport_selection.type = ViewportSelectionRequest::Type::Box;
                actions->viewport_selection.normalized_min_x =
                    std::clamp((std::min(drag_selection_.start.x, drag_selection_.current.x) - viewport_rect_min.x) / std::max(render_size_.x, 1.0F), 0.0F, 1.0F);
                actions->viewport_selection.normalized_min_y =
                    std::clamp((std::min(drag_selection_.start.y, drag_selection_.current.y) - viewport_rect_min.y) / std::max(render_size_.y, 1.0F), 0.0F, 1.0F);
                actions->viewport_selection.normalized_max_x =
                    std::clamp((std::max(drag_selection_.start.x, drag_selection_.current.x) - viewport_rect_min.x) / std::max(render_size_.x, 1.0F), 0.0F, 1.0F);
                actions->viewport_selection.normalized_max_y =
                    std::clamp((std::max(drag_selection_.start.y, drag_selection_.current.y) - viewport_rect_min.y) / std::max(render_size_.y, 1.0F), 0.0F, 1.0F);
                actions->viewport_selection.toggle_existing = drag_selection_.toggle_existing;
            } else {
                actions->viewport_selection.type = ViewportSelectionRequest::Type::Click;
                actions->viewport_selection.normalized_x =
                    std::clamp((drag_selection_.current.x - viewport_rect_min.x) / std::max(render_size_.x, 1.0F), 0.0F, 1.0F);
                actions->viewport_selection.normalized_y =
                    std::clamp((drag_selection_.current.y - viewport_rect_min.y) / std::max(render_size_.y, 1.0F), 0.0F, 1.0F);
                actions->viewport_selection.toggle_existing = drag_selection_.toggle_existing;
            }

            drag_selection_.active = false;
            drag_selection_.toggle_existing = false;
        }
    }

    if (
        actions != nullptr &&
        viewport_interaction_hovered &&
        clicked_display_item == -1 &&
        clicked_filter_item == -1 &&
        !gizmo_result.hovered &&
        !gizmo_result.context_open
    ) {
        ImGuiIO& io = ImGui::GetIO();
        const bool mouse_over_controls =
            pointInRect(io.MousePos, display_controls_min, display_controls_max) ||
            pointInRect(io.MousePos, filter_controls_min, filter_controls_max);

        if (viewport_left_clicked && !mouse_over_controls) {
            drag_selection_.active = true;
            drag_selection_.toggle_existing = isCmdOrCtrlHeld(window_, io);
            drag_selection_.start = clampToRect(io.MousePos, viewport_rect_min, viewport_rect_max);
            drag_selection_.current = drag_selection_.start;
        }

        if (io.MouseWheel != 0.0F) {
            const float zoom_direction = viewport_control_settings.invert_zoom ? -1.0F : 1.0F;
            actions->viewport_camera.zoom_delta += io.MouseWheel * zoom_direction;
        }

        const bool pan_with_modifier =
            (viewport_control_settings.pan_modifier == PanModifier::Shift && io.KeyShift) ||
            (viewport_control_settings.pan_modifier == PanModifier::CmdOrCtrl && isCmdOrCtrlHeld(window_, io));
        const bool pan_with_right_click_only =
            viewport_control_settings.pan_modifier == PanModifier::RightClick;

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) && (pan_with_modifier || pan_with_right_click_only)) {
            actions->viewport_camera.pan_delta.x += io.MouseDelta.x;
            actions->viewport_camera.pan_delta.y += io.MouseDelta.y;
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            actions->viewport_camera.orbit_delta.x += io.MouseDelta.x;
            const float y_direction = viewport_control_settings.invert_y_movement ? -1.0F : 1.0F;
            actions->viewport_camera.orbit_delta.y += io.MouseDelta.y * y_direction;
        }
    }

    if (drag_selection_.active) {
        const ImVec2 selection_min = ImVec2(
            std::min(drag_selection_.start.x, drag_selection_.current.x),
            std::min(drag_selection_.start.y, drag_selection_.current.y)
        );
        const ImVec2 selection_max = ImVec2(
            std::max(drag_selection_.start.x, drag_selection_.current.x),
            std::max(drag_selection_.start.y, drag_selection_.current.y)
        );
        draw_list->AddRectFilled(selection_min, selection_max, IM_COL32(196, 200, 206, 42));
        draw_list->AddRect(selection_min, selection_max, IM_COL32(214, 218, 224, 220), 0.0F, 0, 1.5F);
    }

    ImGui::End();
}

void ViewportPane::setTexture(std::uint32_t texture_id) {
    texture_id_ = texture_id;
}

ImVec2 ViewportPane::renderSize() const {
    return render_size_;
}

ImVec2 ViewportPane::renderTargetSize(const GraphicsQualitySettings& graphics_quality_settings) const {
    const float scale = static_cast<float>(graphics_quality_settings.render_resolution_percent) / 100.0F;
    return ImVec2(
        std::max(render_size_.x * framebuffer_scale_.x * scale, 1.0F),
        std::max(render_size_.y * framebuffer_scale_.y * scale, 1.0F)
    );
}

int ViewportPane::drawSegmentedControl(
    const char* id,
    const ImVec2& top_right,
    std::span<const SegmentedControlItem> items
) const {
    constexpr ImVec4 inactive_button = ImVec4(0.30F, 0.33F, 0.37F, 1.0F);
    constexpr ImVec4 inactive_hovered = ImVec4(0.36F, 0.39F, 0.44F, 1.0F);
    constexpr ImVec4 inactive_text = ImVec4(0.88F, 0.90F, 0.93F, 1.0F);
    constexpr ImVec4 active_button = ImVec4(0.18F, 0.28F, 0.40F, 1.0F);
    constexpr ImVec4 active_hovered = ImVec4(0.22F, 0.33F, 0.46F, 1.0F);
    constexpr ImVec4 active_text = ImVec4(0.94F, 0.97F, 1.0F, 1.0F);

    int clicked_index = -1;
    ImGui::PushID(id);
    for (std::size_t index = 0; index < items.size(); ++index) {
        const SegmentedControlItem& item = items[index];
        const ImVec2 button_position = ImVec2(
            top_right.x - kOverlayButtonSize.x,
            top_right.y + (static_cast<float>(index) * kOverlayButtonSize.y)
        );

        ImGui::SetCursorScreenPos(button_position);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0F);
        ImGui::PushStyleColor(ImGuiCol_Button, item.selected ? active_button : inactive_button);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, item.selected ? active_hovered : inactive_hovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, item.selected ? active_hovered : inactive_hovered);
        ImGui::PushStyleColor(ImGuiCol_Text, item.selected ? active_text : inactive_text);

        if (ImGui::Button(item.label, kOverlayButtonSize)) {
            clicked_index = static_cast<int>(index);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(item.tooltip);
            if (!item.shortcut.empty()) {
                ImGui::SameLine(0.0F, 6.0F);
                ImGui::TextDisabled("%.*s", static_cast<int>(item.shortcut.size()), item.shortcut.data());
            }
            ImGui::EndTooltip();
        }

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
    }
    ImGui::PopID();
    return clicked_index;
}

}  // namespace meshtools::ui
