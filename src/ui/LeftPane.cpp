#include "meshtools/ui/LeftPane.h"

#include "meshtools/ui/EditorDockLayout.h"

namespace meshtools::ui {

void LeftPane::draw(const EditorUiState& state) const {
    constexpr ImGuiWindowFlags pane_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove;

    ImGui::Begin(kEditorLeftPaneWindowName, nullptr, pane_flags);
    ImGui::TextUnformatted("Scene");
    ImGui::Separator();
    if (state.active_document != nullptr) {
        ImGui::Selectable(state.active_document->displayName().c_str(), true);
    } else {
        ImGui::TextDisabled("No mesh loaded");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Inspector");
    if (state.active_document != nullptr) {
        const mesh::MeshDocument& document = *state.active_document;
        ImGui::Text("Path: %s", document.source_path.string().c_str());
        ImGui::Text("Format: %s", document.formatLabel().c_str());
        ImGui::Text("Vertices: %zu", document.positions.size());
        ImGui::Text("Triangles: %zu", document.triangles.size());
        ImGui::Text("Normals: %zu", document.normals.size());

        if (document.bounds.valid) {
            ImGui::SeparatorText("Bounds");
            ImGui::Text("Min: %.3f %.3f %.3f", document.bounds.minimum.x, document.bounds.minimum.y, document.bounds.minimum.z);
            ImGui::Text("Max: %.3f %.3f %.3f", document.bounds.maximum.x, document.bounds.maximum.y, document.bounds.maximum.z);
        }
    } else {
        ImGui::TextWrapped("Use File > Open to load an OBJ or STL mesh.");
    }

    ImGui::End();
}

}  // namespace meshtools::ui
