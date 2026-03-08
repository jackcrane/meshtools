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
    void openMeshDocument();
    void loadMeshDocument(const std::filesystem::path& path);
    void loadStartupSampleIfPresent();
    void applyViewportCameraInput(const ui::ViewportCameraInput& input);

    AppConfig config_;
    platform::GlfwWindow window_;
    render::ViewportRenderer viewport_renderer_;
    ui::EditorUi editor_ui_;
    std::optional<mesh::MeshDocument> active_document_;
    std::vector<std::string> log_messages_;
    ui::ViewportCameraInput pending_viewport_camera_input_;
};

}  // namespace meshtools::app
