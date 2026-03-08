#include "meshtools/ui/EditorUi.h"

#include "meshtools/ui/ViewportCommands.h"

namespace meshtools::ui {

EditorUi::EditorUi(GLFWwindow* window, const char* glsl_version)
    : imgui_system_(window, glsl_version),
      viewport_pane_(window) {
    sequential_shortcuts_.registerShortcut(ImGuiKey_V, ImGuiKey_W, "Toggle wireframe", ShortcutCommand::ToggleWireframe);
    sequential_shortcuts_.registerShortcut(ImGuiKey_V, ImGuiKey_S, "Toggle shade tris", ShortcutCommand::ToggleShadeTriangles);
    sequential_shortcuts_.registerShortcut(ImGuiKey_V, ImGuiKey_P, "Toggle show points", ShortcutCommand::ToggleShowPoints);
    sequential_shortcuts_.registerShortcut(ImGuiKey_F, ImGuiKey_E, "Toggle edge selection", ShortcutCommand::ToggleEdgeSelection);
    sequential_shortcuts_.registerShortcut(ImGuiKey_F, ImGuiKey_F, "Toggle face selection", ShortcutCommand::ToggleFaceSelection);
    sequential_shortcuts_.registerShortcut(ImGuiKey_F, ImGuiKey_P, "Toggle point selection", ShortcutCommand::TogglePointSelection);
    sequential_shortcuts_.registerShortcut(ImGuiKey_V, ImGuiKey_R, "Reset viewport", ShortcutCommand::ResetViewport);
}

EditorUi::~EditorUi() = default;

void EditorUi::beginFrame() const {
    imgui_system_.beginFrame();
}

EditorUiActions EditorUi::draw(const EditorUiState& state) {
    EditorUiActions actions;
    sequential_shortcuts_.handleInput([this, &actions](ShortcutCommand command) {
        triggerShortcutAction(command, &actions);
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
        &actions,
        [this, &actions]() { toggleWireframe(viewport_display_settings_, &actions); },
        [this, &actions]() { toggleShadeTriangles(viewport_display_settings_, &actions); },
        [this, &actions]() { toggleShowPoints(viewport_display_settings_, &actions); },
        [this, &actions]() { toggleEdgeSelection(selection_filters_, &actions); },
        [this, &actions]() { toggleFaceSelection(selection_filters_, &actions); },
        [this, &actions]() { togglePointSelection(selection_filters_, &actions); }
    );
    settings_window_.draw(
        viewport_control_settings_,
        graphics_quality_settings_,
        file_import_settings_,
        &actions
    );
    sequential_shortcuts_.drawMenu([this, &actions](ShortcutCommand command) {
        triggerShortcutAction(command, &actions);
    });

    return actions;
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
    return clear_color_;
}

const FileImportSettings& EditorUi::fileImportSettings() const {
    return file_import_settings_;
}

const ViewportDisplaySettings& EditorUi::viewportDisplaySettings() const {
    return viewport_display_settings_;
}

ImVec2 EditorUi::viewportRenderSize() const {
    return viewport_pane_.renderSize();
}

ImVec2 EditorUi::viewportRenderTargetSize() const {
    return viewport_pane_.renderTargetSize(graphics_quality_settings_);
}

void EditorUi::triggerShortcutAction(ShortcutCommand action, EditorUiActions* actions) {
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
    }
}

}  // namespace meshtools::ui
