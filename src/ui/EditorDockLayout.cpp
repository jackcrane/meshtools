#include "meshtools/ui/EditorDockLayout.h"

#include "imgui_internal.h"

namespace meshtools::ui {

void EditorDockLayout::draw() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags dockspace_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    constexpr ImGuiDockNodeFlags docknode_flags = ImGuiDockNodeFlags_PassthruCentralNode;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::Begin(kEditorDockspaceWindowName, nullptr, dockspace_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID(kEditorDockspaceName);
    if (!layout_initialized_) {
        layout_initialized_ = true;

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        ImGuiID main_dock_id = dockspace_id;
        ImGuiID left_dock_id = ImGui::DockBuilderSplitNode(main_dock_id, ImGuiDir_Left, 0.24F, nullptr, &main_dock_id);
        ImGuiID bottom_dock_id = ImGui::DockBuilderSplitNode(main_dock_id, ImGuiDir_Down, 0.26F, nullptr, &main_dock_id);

        ImGui::DockBuilderDockWindow(kEditorLeftPaneWindowName, left_dock_id);
        ImGui::DockBuilderDockWindow(kEditorBottomPaneWindowName, bottom_dock_id);
        ImGui::DockBuilderDockWindow(kEditorViewportWindowName, main_dock_id);

        constexpr ImGuiDockNodeFlags locked_node_flags =
            ImGuiDockNodeFlags_NoDockingSplit |
            ImGuiDockNodeFlags_NoResize |
            ImGuiDockNodeFlags_NoUndocking |
            ImGuiDockNodeFlags_NoTabBar;

        if (ImGuiDockNode* left_node = ImGui::DockBuilderGetNode(left_dock_id); left_node != nullptr) {
            left_node->LocalFlags |= locked_node_flags;
        }
        if (ImGuiDockNode* bottom_node = ImGui::DockBuilderGetNode(bottom_dock_id); bottom_node != nullptr) {
            bottom_node->LocalFlags |= locked_node_flags;
        }
        if (ImGuiDockNode* main_node = ImGui::DockBuilderGetNode(main_dock_id); main_node != nullptr) {
            main_node->LocalFlags |= locked_node_flags;
        }

        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0.0F, 0.0F), docknode_flags);
    ImGui::End();
}

}  // namespace meshtools::ui
