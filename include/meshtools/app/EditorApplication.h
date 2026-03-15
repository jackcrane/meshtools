#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "meshtools/mesh/MeshDocument.h"
#include "meshtools/platform/GlfwWindow.h"
#include "meshtools/render/ViewportRenderer.h"
#include "meshtools/ui/EditorUi.h"

namespace meshtools::app {

struct AppConfig {
    std::string name = "MeshTools";
    int width = 1600;
    int height = 900;
};

class EditorApplication {
  public:
    explicit EditorApplication(AppConfig config = {});

    int run();

    struct EditableDocumentState {
        mesh::UpAxis up_axis = mesh::UpAxis::Y;
        std::uint64_t mesh_revision = 0;
        std::vector<mesh::Vec3> positions;
        std::vector<mesh::Vec3> normals;
        std::vector<mesh::Triangle> triangles;
        std::vector<mesh::EdgeSegment> explicit_edges;
        mesh::Bounds bounds;
        std::vector<mesh::EntitySet> entity_sets;
    };

    struct DocumentHistorySnapshot {
        EditableDocumentState document_state;
        mesh::EntitySelection selection;
        std::optional<std::size_t> selected_entity_set_index;
    };

    struct DocumentHistoryNode {
        std::size_t node_id = 0;
        std::optional<std::size_t> parent_index;
        std::vector<std::size_t> child_indices;
        std::optional<std::size_t> preferred_child_index;
        std::string label;
        DocumentHistorySnapshot snapshot;
    };

  private:
    void appendLog(std::string origin, std::string message);
    void openDocument();
    void openPath(const std::filesystem::path& path);
    void saveProject();
    void saveProjectAs();
    void loadMeshDocument(const std::filesystem::path& path);
    void loadProjectDocument(const std::filesystem::path& path);
    void applyViewportCameraInput(const ui::ViewportCameraInput& input);
    void handleProjectActions(const ui::EditorUiActions& actions);
    void handleEntitySetActions(const ui::EditorUiActions& actions);
    void handleExpandSelectionActions(const ui::EditorUiActions& actions);
    void handleInvertSelectionRequest();
    void handleSelectSimilarActions(const ui::EditorUiActions& actions);
    void handleSelectEdgeLoopRequest();
    void handleModifyCreateFaceRequest(const ui::EditorUiActions& actions);
    void handleModifyProjectRequest(const ui::EditorUiActions& actions);
    void handleModifyDeleteRequest(const ui::EditorUiActions& actions);
    void handleViewportSelectionRequest(const ui::ViewportSelectionRequest& request);
    void createEntitySetFromCurrentSelection();
    void createEntitySetFromSelection(mesh::EntitySelection selection);
    void addCurrentSelectionToEntitySet(std::size_t index);
    void addSelectionToEntitySet(std::size_t index, mesh::EntitySelection selection);
    void selectEntitySet(std::size_t index);
    void bumpMeshRevision();
    void resetDocumentHistory(std::string root_label);
    void commitDocumentHistory(
        std::string label,
        const mesh::EntitySelection& selection,
        std::optional<std::size_t> selected_entity_set_index
    );
    void undoDocumentHistory();
    void redoDocumentHistory();
    void jumpToDocumentHistoryNode(std::size_t node_id);
    void restoreDocumentHistoryNode(std::size_t node_index, std::string action);
    void rebuildDocumentHistoryUiState();
    void setPreferredHistoryPathToNode(std::size_t node_index);
    [[nodiscard]] DocumentHistorySnapshot captureDocumentHistorySnapshot(
        const mesh::EntitySelection& selection,
        std::optional<std::size_t> selected_entity_set_index
    ) const;
    [[nodiscard]] std::optional<std::size_t> historyNodeIndexById(std::size_t node_id) const;
    [[nodiscard]] std::string makeDefaultEntitySetName() const;

    AppConfig config_;
    platform::GlfwWindow window_;
    render::ViewportRenderer viewport_renderer_;
    ui::EditorUi editor_ui_;
    std::optional<mesh::MeshDocument> active_document_;
    std::filesystem::path active_project_path_;
    std::optional<std::size_t> selected_entity_set_index_;
    std::vector<std::string> log_messages_;
    std::vector<std::string> expand_selection_feedback_reasons_;
    std::vector<std::string> select_similar_feedback_reasons_;
    ui::EditorUiState::ExpandSelectionFeedback expand_selection_feedback_{};
    ui::EditorUiState::SelectSimilarFeedback select_similar_feedback_{};
    ui::ViewportCameraInput pending_viewport_camera_input_;
    std::optional<mesh::EntitySelection> pending_renderer_selection_;
    std::vector<DocumentHistoryNode> document_history_;
    std::optional<std::size_t> current_history_index_;
    std::size_t next_history_node_id_ = 1;
    std::uint64_t next_mesh_revision_id_ = 1;
    std::vector<ui::EditorUiState::HistoryEntry> history_entries_;
    std::vector<ui::EditorUiState::HistoryBranchEntry> active_history_branch_;
    std::size_t active_history_branch_position_ = 0;
};

}  // namespace meshtools::app
