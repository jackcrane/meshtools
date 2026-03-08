#include "meshtools/app/EditorApplication.h"

#include <utility>

#include "meshtools/io/MeshImporter.h"
#include "meshtools/platform/FileDialog.h"

namespace meshtools::app {

EditorApplication::EditorApplication(AppConfig config)
    : config_(std::move(config)),
      window_(platform::WindowConfig{.title = config_.name, .width = config_.width, .height = config_.height}),
      editor_ui_(window_.nativeHandle(), window_.glslVersion()) {
    appendLog("Ready. Use Open to load an OBJ or STL mesh.");
    appendLog("Viewport controls: right drag orbits, shift-right drag pans, scroll zooms, R resets.");
    loadStartupSampleIfPresent();
}

int EditorApplication::run() {
    while (!window_.shouldClose()) {
        window_.pollEvents();
        applyViewportCameraInput(pending_viewport_camera_input_);

        const ImVec2 viewport_render_size = editor_ui_.viewportRenderTargetSize();
        viewport_renderer_.render(
            active_document_ ? &active_document_.value() : nullptr,
            static_cast<int>(viewport_render_size.x),
            static_cast<int>(viewport_render_size.y),
            render::ViewportRenderer::DisplaySettings{
                .show_wireframe = editor_ui_.viewportDisplaySettings().show_wireframe,
                .shade_triangles = editor_ui_.viewportDisplaySettings().shade_triangles,
            }
        );
        editor_ui_.setViewportTexture(viewport_renderer_.textureId());

        const ImVec4& clear_color = editor_ui_.clearColor();
        window_.beginFrame(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        editor_ui_.beginFrame();

        const ui::EditorUiState ui_state{
            .active_document = active_document_ ? &active_document_.value() : nullptr,
            .log_messages = log_messages_,
            .camera_yaw = viewport_renderer_.camera().yaw,
            .camera_pitch = viewport_renderer_.camera().pitch,
        };
        const ui::EditorUiActions actions = editor_ui_.draw(ui_state);
        editor_ui_.endFrame(window_.nativeHandle());
        window_.swapBuffers();

        pending_viewport_camera_input_ = actions.viewport_camera;

        if (actions.request_open_mesh) {
            openMeshDocument();
        }

        if (actions.request_exit) {
            window_.requestClose();
        }
    }

    return 0;
}

void EditorApplication::appendLog(std::string message) {
    log_messages_.push_back(std::move(message));
    constexpr std::size_t max_log_messages = 200;
    if (log_messages_.size() > max_log_messages) {
        const auto overflow =
            static_cast<std::vector<std::string>::difference_type>(log_messages_.size() - max_log_messages);
        log_messages_.erase(log_messages_.begin(), log_messages_.begin() + overflow);
    }
}

void EditorApplication::openMeshDocument() {
    const std::optional<std::filesystem::path> selected_path = platform::openMeshFileDialog();
    if (!selected_path.has_value()) {
        appendLog("Open canceled.");
        return;
    }

    loadMeshDocument(*selected_path);
}

void EditorApplication::loadMeshDocument(const std::filesystem::path& path) {
    io::MeshImportResult result = io::importMeshFromFile(path);
    if (!result.succeeded()) {
        appendLog("Failed to load mesh: " + result.error_message);
        return;
    }

    active_document_ = std::move(result.document);
    appendLog(
        "Loaded " + active_document_->displayName() +
        " (" + active_document_->formatLabel() + ", " +
        std::to_string(active_document_->positions.size()) + " vertices, " +
        std::to_string(active_document_->triangles.size()) + " triangles)."
    );
}

void EditorApplication::loadStartupSampleIfPresent() {
    const std::filesystem::path sample_path = std::filesystem::path("samples") / "Stanford_Bunny_sample.stl";
    if (!std::filesystem::exists(sample_path)) {
        appendLog("Startup sample not found: " + sample_path.string());
        return;
    }

    loadMeshDocument(sample_path);
}

void EditorApplication::applyViewportCameraInput(const ui::ViewportCameraInput& input) {
    if (input.reset) {
        viewport_renderer_.resetCamera();
    }

    if (input.orbit_delta.x != 0.0F || input.orbit_delta.y != 0.0F) {
        viewport_renderer_.orbit(input.orbit_delta.x, input.orbit_delta.y);
    }

    if (input.pan_delta.x != 0.0F || input.pan_delta.y != 0.0F) {
        viewport_renderer_.pan(input.pan_delta.x, input.pan_delta.y);
    }

    if (input.zoom_delta != 0.0F) {
        viewport_renderer_.zoom(input.zoom_delta);
    }
}

}  // namespace meshtools::app
