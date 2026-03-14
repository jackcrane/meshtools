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

  private:
    void appendLog(std::string origin, std::string message);
    void openDocument();
    void openPath(const std::filesystem::path& path);
    void saveProject();
    void saveProjectAs();
    void loadMeshDocument(const std::filesystem::path& path);
    void loadProjectDocument(const std::filesystem::path& path);
    void applyViewportCameraInput(const ui::ViewportCameraInput& input);
    void handleEntitySetActions(const ui::EditorUiActions& actions);
    void handleExpandSelectionActions(const ui::EditorUiActions& actions);
    void handleInvertSelectionRequest();
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
    ui::EditorUiState::ExpandSelectionFeedback expand_selection_feedback_{};
    ui::ViewportCameraInput pending_viewport_camera_input_;
    std::optional<mesh::EntitySelection> pending_renderer_selection_;
};

}  // namespace meshtools::app
