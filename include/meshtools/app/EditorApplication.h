#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "meshtools/app/DocumentHistoryController.h"
#include "meshtools/app/EditorDocumentState.h"
#include "meshtools/app/EditorLogger.h"
#include "meshtools/mesh/MeshDocument.h"
#include "meshtools/mesh/MeshOperations/Detail.h"
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

    struct ModifyAvailabilityCache {
        bool valid = false;
        std::filesystem::path source_path;
        std::uint64_t mesh_revision = 0;
        std::size_t vertex_count = 0;
        std::size_t triangle_count = 0;
        std::size_t explicit_edge_count = 0;
        mesh::EntitySelection selection;
        mesh::ModifyDeleteAvailability modify_delete_availability;
        mesh::ModifyCreateFaceAvailability modify_create_face_availability;
        mesh::ModifyProjectAvailability modify_project_availability;
    };

    struct DocumentTopologyCache {
        bool valid = false;
        std::filesystem::path source_path;
        std::uint64_t mesh_revision = 0;
        std::size_t vertex_count = 0;
        std::size_t triangle_count = 0;
        std::size_t explicit_edge_count = 0;
        mesh::operations::detail::MeshTopology topology;
    };

  private:
    struct DocumentLoadOutcome;
    struct AsyncDocumentLoadState;
    struct PendingDocumentLoad;
    struct MeshOperationOutcome;
    struct AsyncMeshOperationState;
    struct PendingMeshOperation;
    struct HistoryRestoreOutcome;
    struct AsyncHistoryRestoreState;
    struct PendingHistoryRestore;
    struct AsyncDocumentTopologyPrecomputeState;
    struct PendingDocumentTopologyPrecompute;

    void appendLog(std::string origin, std::string message);
    void openDocument();
    void openPath(const std::filesystem::path& path);
    void saveProject();
    void saveProjectAs();
    void beginDocumentLoad(const std::filesystem::path& path);
    void pollPendingDocumentLoad();
    void beginModifyCreateFaceOperation();
    void beginModifyProjectOperation(const mesh::ModifyProjectOptions& options);
    void beginModifyDeleteOperation(const mesh::ModifyDeleteOptions& options);
    void startPendingMeshOperation();
    void pollPendingMeshOperation();
    void beginHistoryRestoreOperation(DocumentHistoryRestoreTarget target, std::string action);
    void startPendingHistoryRestore();
    void pollPendingHistoryRestore();
    void beginDocumentTopologyPrecompute(std::string title, std::string message);
    void startPendingDocumentTopologyPrecompute();
    void pollPendingDocumentTopologyPrecompute();
    void applyLoadedMeshDocument(DocumentLoadOutcome outcome);
    void applyLoadedProjectDocument(const std::filesystem::path& path, DocumentLoadOutcome outcome);
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
    [[nodiscard]] std::string makeDefaultEntitySetName() const;

    struct DocumentLoadOutcome {
        std::optional<mesh::MeshDocument> document;
        std::vector<std::string> log_messages;
        std::string error_message;
    };

    struct AsyncDocumentLoadState {
        std::mutex mutex;
        bool completed = false;
        DocumentLoadOutcome outcome;
    };

    struct PendingDocumentLoad {
        enum class Kind {
            Mesh,
            Project,
        };

        Kind kind = Kind::Mesh;
        std::filesystem::path path;
        bool show_dialog = false;
        std::shared_ptr<AsyncDocumentLoadState> state;
        std::jthread worker;
    };

    struct MeshOperationOutcome {
        bool changed = false;
        EditableDocumentState document_state;
        mesh::EntitySelection selection_after;
        std::optional<std::size_t> selected_entity_set_index;
        std::string skipped_log_message;
        std::string success_log_message;
        std::string history_label;
        std::string cache_title;
        std::string cache_message;
    };

    struct AsyncMeshOperationState {
        std::mutex mutex;
        bool completed = false;
        MeshOperationOutcome outcome;
    };

    struct PendingMeshOperation {
        enum class Kind {
            CreateFace,
            Project,
            Delete,
        };

        Kind kind = Kind::Delete;
        std::string title;
        std::string message;
        bool started = false;
        mesh::MeshDocument document_snapshot;
        mesh::EntitySelection selection;
        mesh::ModifyProjectOptions project_options;
        mesh::ModifyDeleteOptions delete_options;
        std::shared_ptr<AsyncMeshOperationState> state;
        std::jthread worker;
    };

    struct HistoryRestoreOutcome {
        bool valid = false;
        DocumentHistoryRestoreTarget target;
        std::string action;
    };

    struct AsyncHistoryRestoreState {
        std::mutex mutex;
        bool completed = false;
        HistoryRestoreOutcome outcome;
    };

    struct PendingHistoryRestore {
        DocumentHistoryRestoreTarget target;
        std::string action;
        std::string title;
        std::string message;
        bool started = false;
        std::shared_ptr<AsyncHistoryRestoreState> state;
        std::jthread worker;
    };

    struct AsyncDocumentTopologyPrecomputeState {
        std::mutex mutex;
        bool completed = false;
        DocumentTopologyCache cache;
    };

    struct PendingDocumentTopologyPrecompute {
        std::string title;
        std::string message;
        bool started = false;
        std::shared_ptr<AsyncDocumentTopologyPrecomputeState> state;
        std::jthread worker;
    };

    AppConfig config_;
    platform::GlfwWindow window_;
    render::ViewportRenderer viewport_renderer_;
    ui::EditorUi editor_ui_;
    std::optional<mesh::MeshDocument> active_document_;
    std::filesystem::path active_project_path_;
    std::optional<std::size_t> selected_entity_set_index_;
    EditorLogger logger_;
    std::vector<std::string> expand_selection_feedback_reasons_;
    std::vector<std::string> select_similar_feedback_reasons_;
    std::vector<ui::EditorUiState::TimedTask> frame_performance_tasks_;
    ui::EditorUiState::ExpandSelectionFeedback expand_selection_feedback_{};
    ui::EditorUiState::SelectSimilarFeedback select_similar_feedback_{};
    ModifyAvailabilityCache modify_availability_cache_{};
    DocumentTopologyCache document_topology_cache_{};
    ui::ViewportCameraInput pending_viewport_camera_input_;
    std::optional<mesh::EntitySelection> pending_renderer_selection_;
    std::optional<PendingDocumentLoad> pending_document_load_;
    std::optional<PendingMeshOperation> pending_mesh_operation_;
    std::optional<PendingHistoryRestore> pending_history_restore_;
    std::optional<PendingDocumentTopologyPrecompute> pending_document_topology_precompute_;
    DocumentHistoryController history_controller_;
    std::uint64_t next_mesh_revision_id_ = 1;
};

}  // namespace meshtools::app
