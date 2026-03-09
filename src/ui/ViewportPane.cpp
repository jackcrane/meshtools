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
constexpr const char* kViewportContextMenuName = "ViewportContextMenu";
constexpr const char* kEntitySetPickerPopupName = "ViewportEntitySetPicker";
constexpr const char* kExpandSelectionDialogName = "Expand Selection";

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

ViewportSelectionRequest::Mode selectionModeFromModifiers(GLFWwindow* window, const ImGuiIO& io) {
    if (io.KeyShift) {
        return ViewportSelectionRequest::Mode::Path;
    }

    return isCmdOrCtrlHeld(window, io)
        ? ViewportSelectionRequest::Mode::Toggle
        : ViewportSelectionRequest::Mode::Replace;
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
    std::string_view add_to_entity_set_shortcut,
    std::string_view expand_selection_shortcut,
    std::string_view invert_selection_shortcut,
    EditorUiActions* actions,
    const std::function<void()>& on_toggle_wireframe,
    const std::function<void()>& on_toggle_shade_triangles,
    const std::function<void()>& on_toggle_show_points,
    const std::function<void()>& on_toggle_edges,
    const std::function<void()>& on_toggle_faces,
    const std::function<void()>& on_toggle_points,
    const std::function<void()>& on_invert_selection
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
    const char* invert_selection_shortcut_label =
        invert_selection_shortcut.empty() ? nullptr : invert_selection_shortcut.data();
    const char* add_to_entity_set_shortcut_label =
        add_to_entity_set_shortcut.empty() ? nullptr : add_to_entity_set_shortcut.data();
    const char* expand_selection_shortcut_label =
        expand_selection_shortcut.empty() ? nullptr : expand_selection_shortcut.data();
    const bool can_add_selection_to_entity_set =
        actions != nullptr &&
        state.active_document != nullptr &&
        state.selection_summary.totalCount() > 0;

    if (actions != nullptr && actions->request_add_selection_to_entity_set) {
        actions->request_add_selection_to_entity_set = false;
        if (can_add_selection_to_entity_set) {
            if (state.entity_sets.empty()) {
                actions->request_create_entity_set_from_selection = true;
            } else {
                queueEntitySetPicker(ImGui::GetMousePos(), EntitySetPickerMode::CurrentSelection);
            }
        }
    }

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
    const bool viewport_context_open = ImGui::IsPopupOpen(kViewportContextMenuName);

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
                actions->viewport_selection.mode =
                    drag_selection_.mode == ViewportSelectionRequest::Mode::Toggle
                        ? ViewportSelectionRequest::Mode::Toggle
                        : ViewportSelectionRequest::Mode::Replace;
            } else {
                actions->viewport_selection.type = ViewportSelectionRequest::Type::Click;
                actions->viewport_selection.mode = drag_selection_.mode;
                actions->viewport_selection.normalized_x =
                    std::clamp((drag_selection_.current.x - viewport_rect_min.x) / std::max(render_size_.x, 1.0F), 0.0F, 1.0F);
                actions->viewport_selection.normalized_y =
                    std::clamp((drag_selection_.current.y - viewport_rect_min.y) / std::max(render_size_.y, 1.0F), 0.0F, 1.0F);
            }

            drag_selection_.active = false;
            drag_selection_.mode = ViewportSelectionRequest::Mode::Replace;
        }
    }

    if (
        actions != nullptr &&
        viewport_interaction_hovered &&
        clicked_display_item == -1 &&
        clicked_filter_item == -1 &&
        !gizmo_result.hovered &&
        !gizmo_result.context_open &&
        !viewport_context_open
    ) {
        ImGuiIO& io = ImGui::GetIO();
        const bool mouse_over_controls =
            pointInRect(io.MousePos, display_controls_min, display_controls_max) ||
            pointInRect(io.MousePos, filter_controls_min, filter_controls_max);

        if (viewport_left_clicked && !mouse_over_controls) {
            drag_selection_.active = true;
            drag_selection_.mode = selectionModeFromModifiers(window_, io);
            drag_selection_.start = clampToRect(io.MousePos, viewport_rect_min, viewport_rect_max);
            drag_selection_.current = drag_selection_.start;
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !mouse_over_controls) {
            right_click_context_eligible_ = true;
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, kSelectionDragThreshold)) {
            right_click_context_eligible_ = false;
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
            !mouse_over_controls &&
            right_click_context_eligible_) {
            ImGui::OpenPopup(kViewportContextMenuName);
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            right_click_context_eligible_ = false;
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

    if (ImGui::BeginPopup(kViewportContextMenuName)) {
        ImGui::BeginDisabled(!can_add_selection_to_entity_set);
        if (ImGui::MenuItem("Add to Entity Set", add_to_entity_set_shortcut_label)) {
            if (state.entity_sets.empty()) {
                if (actions != nullptr) {
                    actions->request_create_entity_set_from_selection = true;
                }
            } else {
                queueEntitySetPicker(ImGui::GetMousePos(), EntitySetPickerMode::CurrentSelection);
            }
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        if (ImGui::MenuItem("Expand selection", expand_selection_shortcut_label)) {
            openExpandSelectionDialog();
        }
        if (ImGui::MenuItem("Invert selection", invert_selection_shortcut_label)) {
            on_invert_selection();
        }
        ImGui::EndPopup();
    }

    if (entity_set_picker_pending_open_) {
        ImGui::SetNextWindowPos(entity_set_picker_anchor_, ImGuiCond_Appearing);
        ImGui::OpenPopup(kEntitySetPickerPopupName);
        entity_set_picker_pending_open_ = false;
    } else if (ImGui::IsPopupOpen(kEntitySetPickerPopupName)) {
        ImGui::SetNextWindowPos(entity_set_picker_anchor_, ImGuiCond_Appearing);
    }

    if (ImGui::BeginPopup(kEntitySetPickerPopupName)) {
        if (ImGui::Selectable("New Entity Set")) {
            if (actions != nullptr) {
                if (entity_set_picker_mode_ == EntitySetPickerMode::ExpandSelection) {
                    actions->request_expand_selection = EditorUiActions::ExpandSelectionRequest{
                        .config = expand_selection_config_,
                        .intent = EditorUiActions::ExpandSelectionRequest::Intent::CreateEntitySet,
                    };
                    close_expand_selection_dialog_ = true;
                } else {
                    actions->request_create_entity_set_from_selection = true;
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        for (std::size_t index = 0; index < state.entity_sets.size(); ++index) {
            const mesh::EntitySet& entity_set = state.entity_sets[index];
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(entity_set.name.c_str())) {
                if (actions != nullptr) {
                    if (entity_set_picker_mode_ == EntitySetPickerMode::ExpandSelection) {
                        actions->request_expand_selection = EditorUiActions::ExpandSelectionRequest{
                            .config = expand_selection_config_,
                            .intent = EditorUiActions::ExpandSelectionRequest::Intent::AddToExistingEntitySet,
                            .entity_set_index = index,
                        };
                        close_expand_selection_dialog_ = true;
                    } else {
                        actions->request_add_selection_to_existing_entity_set_index = index;
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }

    drawExpandSelectionDialog(state, actions);

    ImGui::End();
}

void ViewportPane::openExpandSelectionDialog() {
    expand_selection_dialog_pending_open_ = true;
    close_expand_selection_dialog_ = false;
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

void ViewportPane::queueEntitySetPicker(const ImVec2& mouse_position, EntitySetPickerMode mode) {
    entity_set_picker_anchor_ = ImVec2(mouse_position.x + 6.0F, mouse_position.y + 6.0F);
    entity_set_picker_pending_open_ = true;
    entity_set_picker_mode_ = mode;
}

void ViewportPane::drawExpandSelectionDialog(const EditorUiState& state, EditorUiActions* actions) {
    if (expand_selection_dialog_pending_open_) {
        ImGui::OpenPopup(kExpandSelectionDialogName);
        expand_selection_dialog_open_ = true;
        expand_selection_dialog_pending_open_ = false;
    }

    if (!expand_selection_dialog_open_ && !ImGui::IsPopupOpen(kExpandSelectionDialogName)) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0F, 0.0F), ImGuiCond_Appearing);
    bool keep_dialog_open = expand_selection_dialog_open_;
    if (ImGui::BeginPopupModal(kExpandSelectionDialogName, &keep_dialog_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (close_expand_selection_dialog_) {
            close_expand_selection_dialog_ = false;
            keep_dialog_open = false;
            expand_selection_dialog_open_ = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        if (actions != nullptr) {
            actions->expand_selection_dialog_open = true;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            keep_dialog_open = false;
            expand_selection_dialog_open_ = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        constexpr const char* method_names[] = {
            "Coplanar",
            "Adjacent",
            "Intersecting Normals",
        };

        int selected_method = 0;
        switch (expand_selection_config_.method) {
            case ExpandSelectionMethod::Coplanar:
                selected_method = 0;
                break;
            case ExpandSelectionMethod::Adjacent:
                selected_method = 1;
                break;
            case ExpandSelectionMethod::IntersectingNormals:
                selected_method = 2;
                break;
        }

        if (ImGui::Combo("Method", &selected_method, method_names, IM_ARRAYSIZE(method_names))) {
            switch (selected_method) {
                case 0:
                    expand_selection_config_.method = ExpandSelectionMethod::Coplanar;
                    break;
                case 1:
                    expand_selection_config_.method = ExpandSelectionMethod::Adjacent;
                    break;
                default:
                    expand_selection_config_.method = ExpandSelectionMethod::IntersectingNormals;
                    break;
            }
        }

        switch (expand_selection_config_.method) {
            case ExpandSelectionMethod::Coplanar:
                ImGui::Checkbox("Include Parallel", &expand_selection_config_.coplanar_include_parallel);
                ImGui::Checkbox("Select adjacent only", &expand_selection_config_.coplanar_select_adjacent_only);
                ImGui::InputFloat("Tolerance (%)", &expand_selection_config_.coplanar_tolerance_percent, 0.001F, 0.01F, "%.4f");
                expand_selection_config_.coplanar_tolerance_percent =
                    std::max(expand_selection_config_.coplanar_tolerance_percent, 0.0F);
                break;
            case ExpandSelectionMethod::Adjacent:
                ImGui::InputFloat("Max angle", &expand_selection_config_.adjacent_max_angle_degrees, 1.0F, 5.0F, "%.2f");
                expand_selection_config_.adjacent_max_angle_degrees =
                    std::clamp(expand_selection_config_.adjacent_max_angle_degrees, 0.0F, 180.0F);
                break;
            case ExpandSelectionMethod::IntersectingNormals:
                ImGui::Checkbox("Include inverse normals", &expand_selection_config_.intersecting_include_inverse_normals);
                ImGui::InputFloat("Tolerance", &expand_selection_config_.intersecting_tolerance, 0.01F, 0.05F, "%.3f");
                expand_selection_config_.intersecting_tolerance =
                    std::max(expand_selection_config_.intersecting_tolerance, 0.0001F);
                if (!state.expand_selection_feedback.linear_intersection_enabled) {
                    expand_selection_config_.intersecting_allow_linear_intersection = false;
                }
                ImGui::BeginDisabled(!state.expand_selection_feedback.linear_intersection_enabled);
                ImGui::Checkbox("Allow linear intersection", &expand_selection_config_.intersecting_allow_linear_intersection);
                ImGui::EndDisabled();
                break;
        }

        if (actions != nullptr) {
            actions->request_expand_selection = EditorUiActions::ExpandSelectionRequest{
                .config = expand_selection_config_,
                .intent = EditorUiActions::ExpandSelectionRequest::Intent::Preview,
            };
        }

        ImGui::Spacing();
        if (state.expand_selection_feedback.available) {
            ImGui::Text(
                "Preview: +%zu faces, %zu total entities",
                state.expand_selection_feedback.preview_added_face_count,
                state.expand_selection_feedback.preview_total_count
            );
        } else if (!state.expand_selection_feedback.unavailable_reasons.empty()) {
            ImGui::SeparatorText("Unavailable");
            for (const std::string& reason : state.expand_selection_feedback.unavailable_reasons) {
                ImGui::TextWrapped("- %s", reason.c_str());
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::BeginDisabled(!state.expand_selection_feedback.available);
        if (ImGui::Button("Select", ImVec2(120.0F, 0.0F)) && actions != nullptr) {
            actions->request_expand_selection = EditorUiActions::ExpandSelectionRequest{
                .config = expand_selection_config_,
                .intent = EditorUiActions::ExpandSelectionRequest::Intent::Select,
            };
            keep_dialog_open = false;
            expand_selection_dialog_open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add to Entity Set", ImVec2(160.0F, 0.0F)) && actions != nullptr) {
            if (state.entity_sets.empty()) {
                actions->request_expand_selection = EditorUiActions::ExpandSelectionRequest{
                    .config = expand_selection_config_,
                    .intent = EditorUiActions::ExpandSelectionRequest::Intent::CreateEntitySet,
                };
                keep_dialog_open = false;
                expand_selection_dialog_open_ = false;
                ImGui::CloseCurrentPopup();
            } else {
                queueEntitySetPicker(ImGui::GetMousePos(), EntitySetPickerMode::ExpandSelection);
            }
        }
        ImGui::EndDisabled();

        expand_selection_dialog_open_ = keep_dialog_open;
        ImGui::EndPopup();
        return;
    }

    expand_selection_dialog_open_ = keep_dialog_open && ImGui::IsPopupOpen(kExpandSelectionDialogName);
}

}  // namespace meshtools::ui
