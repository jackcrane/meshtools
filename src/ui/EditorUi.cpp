#include "meshtools/ui/EditorUi.h"

#include "meshtools/ui/ViewportCommands.h"

namespace meshtools::ui {
namespace {

bool isCmdOrCtrlHeld(const ImGuiIO& io) {
#if defined(__APPLE__)
    return io.KeySuper;
#else
    return io.KeyCtrl;
#endif
}

}  // namespace

EditorUi::EditorUi(GLFWwindow* window, const char* glsl_version)
    : imgui_system_(window, glsl_version),
      viewport_pane_(window) {
    sequential_shortcuts_.registerShortcut(ImGuiKey_V, ImGuiKey_W, "Toggle wireframe", ShortcutCommand::ToggleWireframe);
    sequential_shortcuts_.registerShortcut(ImGuiKey_V, ImGuiKey_S, "Toggle shade tris", ShortcutCommand::ToggleShadeTriangles);
    sequential_shortcuts_.registerShortcut(ImGuiKey_V, ImGuiKey_P, "Toggle show points", ShortcutCommand::ToggleShowPoints);
    sequential_shortcuts_.registerShortcut(ImGuiKey_F, ImGuiKey_E, "Toggle edge selection", ShortcutCommand::ToggleEdgeSelection);
    sequential_shortcuts_.registerShortcut(ImGuiKey_F, ImGuiKey_F, "Toggle face selection", ShortcutCommand::ToggleFaceSelection);
    sequential_shortcuts_.registerShortcut(ImGuiKey_F, ImGuiKey_P, "Toggle point selection", ShortcutCommand::TogglePointSelection);
    sequential_shortcuts_.registerShortcut(ImGuiKey_S, ImGuiKey_E, "Add to Entity Set", ShortcutCommand::AddSelectionToEntitySet);
    sequential_shortcuts_.registerShortcut(ImGuiKey_S, ImGuiKey_L, "Select edge loop", ShortcutCommand::SelectEdgeLoop, true);
    sequential_shortcuts_.registerShortcut(ImGuiKey_S, ImGuiKey_S, "Select similar", ShortcutCommand::SelectSimilar);
    sequential_shortcuts_.registerShortcut(ImGuiKey_S, ImGuiKey_X, "Expand selection", ShortcutCommand::ExpandSelection);
    sequential_shortcuts_.registerShortcut(ImGuiKey_S, ImGuiKey_I, "Invert selection", ShortcutCommand::InvertSelection);
    sequential_shortcuts_.registerShortcut(ImGuiKey_M, ImGuiKey_D, "Modify Delete", ShortcutCommand::ModifyDelete);
    sequential_shortcuts_.registerShortcut(ImGuiKey_M, ImGuiKey_F, "Modify Create Face", ShortcutCommand::ModifyCreateFace);
    sequential_shortcuts_.registerShortcut(ImGuiKey_M, ImGuiKey_P, "Modify Project", ShortcutCommand::ModifyProject);
    sequential_shortcuts_.registerShortcut(ImGuiKey_V, ImGuiKey_R, "Reset viewport", ShortcutCommand::ResetViewport);
}

EditorUi::~EditorUi() = default;

void EditorUi::beginFrame() const {
    imgui_system_.beginFrame();
}

EditorUiActions EditorUi::draw(const EditorUiState& state) {
    EditorUiActions actions;
    handleGlobalShortcuts(&actions);
    sequential_shortcuts_.handleInput([this, &state, &actions](ShortcutCommand command) {
        triggerShortcutAction(command, state, &actions);
    });
    const std::string wireframe_shortcut =
        sequential_shortcuts_.shortcutLabel(ShortcutCommand::ToggleWireframe);
    const std::string shade_triangles_shortcut =
        sequential_shortcuts_.shortcutLabel(ShortcutCommand::ToggleShadeTriangles);
    const std::string show_points_shortcut =
        sequential_shortcuts_.shortcutLabel(ShortcutCommand::ToggleShowPoints);
    const std::string edge_shortcut =
        sequential_shortcuts_.shortcutLabel(ShortcutCommand::ToggleEdgeSelection);
    const std::string face_shortcut =
        sequential_shortcuts_.shortcutLabel(ShortcutCommand::ToggleFaceSelection);
    const std::string point_shortcut =
        sequential_shortcuts_.shortcutLabel(ShortcutCommand::TogglePointSelection);
    const std::string add_to_entity_set_shortcut =
        sequential_shortcuts_.shortcutLabel(ShortcutCommand::AddSelectionToEntitySet);
    const std::string select_similar_shortcut =
        sequential_shortcuts_.shortcutLabel(ShortcutCommand::SelectSimilar);
    const std::string expand_selection_shortcut =
        sequential_shortcuts_.shortcutLabel(ShortcutCommand::ExpandSelection);
    const std::string invert_selection_shortcut =
        sequential_shortcuts_.shortcutLabel(ShortcutCommand::InvertSelection);
    const std::string modify_delete_shortcut =
        sequential_shortcuts_.shortcutLabel(ShortcutCommand::ModifyDelete);
    const std::string modify_create_face_shortcut =
        sequential_shortcuts_.shortcutLabel(ShortcutCommand::ModifyCreateFace);
    const std::string modify_project_shortcut =
        sequential_shortcuts_.shortcutLabel(ShortcutCommand::ModifyProject);

    dock_layout_.draw();
    left_pane_.draw(state, &actions);
    bottom_pane_.draw(state);
    viewport_pane_.draw(
        state,
        viewport_control_settings_,
        file_import_settings_,
        viewport_display_settings_,
        selection_filters_,
        wireframe_shortcut,
        shade_triangles_shortcut,
        show_points_shortcut,
        edge_shortcut,
        face_shortcut,
        point_shortcut,
        add_to_entity_set_shortcut,
        select_similar_shortcut,
        expand_selection_shortcut,
        invert_selection_shortcut,
        modify_delete_shortcut,
        modify_create_face_shortcut,
        modify_project_shortcut,
        &actions,
        [this, &actions]() { toggleWireframe(viewport_display_settings_, &actions); },
        [this, &actions]() { toggleShadeTriangles(viewport_display_settings_, &actions); },
        [this, &actions]() { toggleShowPoints(viewport_display_settings_, &actions); },
        [this, &actions]() { toggleEdgeSelection(selection_filters_, &actions); },
        [this, &actions]() { toggleFaceSelection(selection_filters_, &actions); },
        [this, &actions]() { togglePointSelection(selection_filters_, &actions); },
        [&actions]() { requestInvertSelection(&actions); }
    );
    settings_window_.draw(
        viewport_control_settings_,
        graphics_quality_settings_,
        file_import_settings_,
        imgui_system_,
        &actions
    );
    sequential_shortcuts_.drawMenu([this, &state, &actions](ShortcutCommand command) {
        triggerShortcutAction(command, state, &actions);
    });

    return actions;
}

void EditorUi::handleGlobalShortcuts(EditorUiActions* actions) const {
    if (actions == nullptr) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || !isCmdOrCtrlHeld(io)) {
        return;
    }

    if ((io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) || ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
        actions->request_redo = true;
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        actions->request_undo = true;
    }
}

void EditorUi::endFrame(GLFWwindow* window) const {
    imgui_system_.endFrame(window);
}

void EditorUi::openSettingsWindow() {
    settings_window_.open();
}

void EditorUi::setViewportTexture(std::uint32_t texture_id) {
    viewport_pane_.setTexture(texture_id);
}

const ImVec4& EditorUi::clearColor() const {
    return imgui_system_.clearColor();
}

const FileImportSettings& EditorUi::fileImportSettings() const {
    return file_import_settings_;
}

const SelectionFilters& EditorUi::selectionFilters() const {
    return selection_filters_;
}

const ViewportDisplaySettings& EditorUi::viewportDisplaySettings() const {
    return viewport_display_settings_;
}

const render::ViewportRenderer::ThemeColors& EditorUi::viewportThemeColors() const {
    return imgui_system_.rendererThemeColors();
}

ImVec2 EditorUi::viewportRenderSize() const {
    return viewport_pane_.renderSize();
}

ImVec2 EditorUi::viewportRenderTargetSize() const {
    return viewport_pane_.renderTargetSize(graphics_quality_settings_);
}

void EditorUi::triggerShortcutAction(ShortcutCommand action, const EditorUiState& state, EditorUiActions* actions) {
    switch (action) {
        case ShortcutCommand::ToggleWireframe:
            toggleWireframe(viewport_display_settings_, actions);
            return;
        case ShortcutCommand::ToggleShadeTriangles:
            toggleShadeTriangles(viewport_display_settings_, actions);
            return;
        case ShortcutCommand::ToggleShowPoints:
            toggleShowPoints(viewport_display_settings_, actions);
            return;
        case ShortcutCommand::ToggleEdgeSelection:
            toggleEdgeSelection(selection_filters_, actions);
            return;
        case ShortcutCommand::ToggleFaceSelection:
            toggleFaceSelection(selection_filters_, actions);
            return;
        case ShortcutCommand::TogglePointSelection:
            togglePointSelection(selection_filters_, actions);
            return;
        case ShortcutCommand::ResetViewport:
            resetViewport(viewport_display_settings_, selection_filters_, actions);
            return;
        case ShortcutCommand::AddSelectionToEntitySet:
            requestAddSelectionToEntitySet(actions);
            return;
        case ShortcutCommand::SelectEdgeLoop:
            requestSelectEdgeLoop(actions);
            return;
        case ShortcutCommand::SelectSimilar:
            viewport_pane_.openSelectSimilarDialog();
            return;
        case ShortcutCommand::ExpandSelection:
            viewport_pane_.openExpandSelectionDialog();
            return;
        case ShortcutCommand::InvertSelection:
            requestInvertSelection(actions);
            return;
        case ShortcutCommand::ModifyDelete:
            if (state.modify_delete_availability.any()) {
                viewport_pane_.openModifyDeleteDialog();
            }
            return;
        case ShortcutCommand::ModifyCreateFace:
            if (state.modify_create_face_availability.any()) {
                actions->request_modify_create_face = true;
            }
            return;
        case ShortcutCommand::ModifyProject:
            if (state.modify_project_availability.any()) {
                viewport_pane_.openModifyProjectDialog();
            } else if (actions != nullptr) {
                actions->event_logs.push_back(EditorUiLogEvent{
                    .origin = "MODIFY",
                    .message =
                        state.modify_project_availability.unavailable_reason.empty()
                            ? "Modify Project unavailable."
                            : state.modify_project_availability.unavailable_reason,
                });
            }
            return;
    }
}

}  // namespace meshtools::ui
