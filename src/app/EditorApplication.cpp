#include "meshtools/app/EditorApplication.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <utility>

#include "meshtools/io/MeshImporter.h"
#include "meshtools/io/ProjectArchive.h"
#include "meshtools/platform/FileDialog.h"
#include "meshtools/platform/NativeMenu.h"

namespace meshtools::app {

namespace {

std::string makeTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &now_time);
#else
    localtime_r(&now_time, &local_time);
#endif

    std::ostringstream stream;
    stream << std::put_time(&local_time, "%H:%M:%S")
           << '.'
           << std::setw(3)
           << std::setfill('0')
           << milliseconds.count();
    return stream.str();
}

std::string lowercaseExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

}  // namespace

EditorApplication::EditorApplication(AppConfig config)
    : config_(std::move(config)),
      window_(platform::WindowConfig{.title = config_.name, .width = config_.width, .height = config_.height}),
      editor_ui_(window_.nativeHandle(), window_.glslVersion()) {
    platform::initializeNativeMenu(config_.name);
    appendLog("APP", "Ready. Use Open to load a .mt project or import an OBJ/STL mesh.");
}

int EditorApplication::run() {
    while (!window_.shouldClose()) {
        window_.pollEvents();

        const platform::NativeMenuActions menu_actions = platform::consumePendingNativeMenuActions();
        if (menu_actions.open_document) {
            openDocument();
        }
        if (!menu_actions.open_sample_path.empty()) {
            openPath(menu_actions.open_sample_path);
        }
        if (menu_actions.save_project) {
            saveProject();
        }
        if (menu_actions.save_project_as) {
            saveProjectAs();
        }
        if (menu_actions.open_settings) {
            editor_ui_.openSettingsWindow();
        }
        if (menu_actions.quit) {
            window_.requestClose();
            continue;
        }

        applyViewportCameraInput(pending_viewport_camera_input_);

        const ImVec2 viewport_render_size = editor_ui_.viewportRenderTargetSize();
        viewport_renderer_.render(
            active_document_ ? &active_document_.value() : nullptr,
            static_cast<int>(viewport_render_size.x),
            static_cast<int>(viewport_render_size.y),
            render::ViewportRenderer::DisplaySettings{
                .show_wireframe = editor_ui_.viewportDisplaySettings().show_wireframe,
                .shade_triangles = editor_ui_.viewportDisplaySettings().shade_triangles,
                .show_points = editor_ui_.viewportDisplaySettings().show_points,
                .source_up_axis =
                    !active_document_.has_value() || active_document_->up_axis == mesh::UpAxis::Y
                        ? render::ViewportRenderer::UpAxis::Y
                        : render::ViewportRenderer::UpAxis::Z,
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
            .selection_summary = ui::EditorUiState::SelectionSummary{
                .edge_count = viewport_renderer_.selectionSummary().edge_count,
                .face_count = viewport_renderer_.selectionSummary().face_count,
                .point_count = viewport_renderer_.selectionSummary().point_count,
            },
        };
        const ui::EditorUiActions actions = editor_ui_.draw(ui_state);
        editor_ui_.endFrame(window_.nativeHandle());
        window_.swapBuffers();

        pending_viewport_camera_input_ = actions.viewport_camera;
        for (const ui::EditorUiLogEvent& event_log : actions.event_logs) {
            appendLog(event_log.origin, event_log.message);
        }
        handleViewportSelectionRequest(actions.viewport_selection);
        if (actions.request_invert_selection) {
            handleInvertSelectionRequest();
        }

        if (actions.request_open_document) {
            openDocument();
        }

        if (actions.request_save_project) {
            saveProject();
        }

        if (actions.request_exit) {
            window_.requestClose();
        }
    }

    return 0;
}

void EditorApplication::appendLog(std::string origin, std::string message) {
    log_messages_.push_back('[' + makeTimestamp() + "][" + std::move(origin) + "] " + std::move(message));
    constexpr std::size_t max_log_messages = 200;
    if (log_messages_.size() > max_log_messages) {
        const auto overflow =
            static_cast<std::vector<std::string>::difference_type>(log_messages_.size() - max_log_messages);
        log_messages_.erase(log_messages_.begin(), log_messages_.begin() + overflow);
    }
}

void EditorApplication::openDocument() {
    const std::optional<std::filesystem::path> selected_path = platform::openDocumentFileDialog();
    if (!selected_path.has_value()) {
        appendLog("PROJECT", "Open canceled.");
        return;
    }

    openPath(*selected_path);
}

void EditorApplication::openPath(const std::filesystem::path& path) {
    if (lowercaseExtension(path) == ".mt") {
        loadProjectDocument(path);
        return;
    }

    loadMeshDocument(path);
}

void EditorApplication::saveProject() {
    if (!active_document_.has_value()) {
        appendLog("PROJECT", "Save skipped because there is no active project.");
        return;
    }

    if (active_project_path_.empty()) {
        saveProjectAs();
        return;
    }

    io::ProjectArchiveSaveResult result = io::saveProjectArchive(
        active_project_path_,
        io::ProjectArchiveSaveInput{
            .document = *active_document_,
            .log_messages = log_messages_,
        }
    );
    if (!result.succeeded()) {
        appendLog("PROJECT", "Failed to save project: " + result.error_message);
        return;
    }

    active_document_->source_path = active_project_path_;
    active_document_->display_name_override = active_project_path_.stem().string();
    appendLog("PROJECT", "Saved project " + active_document_->displayName() + '.');
}

void EditorApplication::saveProjectAs() {
    if (!active_document_.has_value()) {
        appendLog("PROJECT", "Save skipped because there is no active project.");
        return;
    }

    std::optional<std::filesystem::path> selected_path = platform::saveProjectFileDialog();
    if (!selected_path.has_value()) {
        appendLog("PROJECT", "Save canceled.");
        return;
    }

    if (lowercaseExtension(*selected_path) != ".mt") {
        selected_path->replace_extension(".mt");
    }

    io::ProjectArchiveSaveResult result = io::saveProjectArchive(
        *selected_path,
        io::ProjectArchiveSaveInput{
            .document = *active_document_,
            .log_messages = log_messages_,
        }
    );
    if (!result.succeeded()) {
        appendLog("PROJECT", "Failed to save project: " + result.error_message);
        return;
    }

    active_project_path_ = *selected_path;
    active_document_->source_path = *selected_path;
    active_document_->display_name_override = selected_path->stem().string();
    appendLog("PROJECT", "Saved project " + active_document_->displayName() + '.');
}

void EditorApplication::loadMeshDocument(const std::filesystem::path& path) {
    io::MeshImportResult result = io::importMeshFromFile(path);
    if (!result.succeeded()) {
        appendLog("IMPORT", "Failed to load mesh: " + result.error_message);
        return;
    }

    active_document_ = std::move(result.document);
    viewport_renderer_.clearSelection();
    active_document_->display_name_override.clear();
    active_document_->up_axis = editor_ui_.fileImportSettings().up_axis;
    active_project_path_.clear();
    appendLog(
        "IMPORT",
        "Loaded " + active_document_->displayName() +
        " (" + active_document_->formatLabel() + ", " +
        std::to_string(active_document_->positions.size()) + " vertices, " +
        std::to_string(active_document_->triangles.size()) + " triangles)."
    );
}

void EditorApplication::loadProjectDocument(const std::filesystem::path& path) {
    io::ProjectArchiveLoadResult result = io::loadProjectArchive(path);
    if (!result.succeeded()) {
        appendLog("PROJECT", "Failed to open project: " + result.error_message);
        return;
    }

    active_document_ = std::move(result.document);
    viewport_renderer_.clearSelection();
    active_project_path_ = path;
    log_messages_ = std::move(result.log_messages);
    appendLog(
        "PROJECT",
        "Opened " + active_document_->displayName() +
        " (" + active_document_->formatLabel() + ", " +
        std::to_string(active_document_->positions.size()) + " vertices, " +
        std::to_string(active_document_->triangles.size()) + " triangles)."
    );
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

void EditorApplication::handleViewportSelectionRequest(const ui::ViewportSelectionRequest& request) {
    if (request.type == ui::ViewportSelectionRequest::Type::None || !active_document_.has_value()) {
        return;
    }

    const ui::SelectionFilters& selection_filters = editor_ui_.selectionFilters();
    const render::ViewportRenderer::SelectionQuery selection_query{
        .edges = selection_filters.edges,
        .faces = selection_filters.faces,
        .points = selection_filters.points,
    };
    const render::ViewportRenderer::SelectionMode selection_mode =
        request.mode == ui::ViewportSelectionRequest::Mode::Toggle
            ? render::ViewportRenderer::SelectionMode::Toggle
            : request.mode == ui::ViewportSelectionRequest::Mode::Path
                  ? render::ViewportRenderer::SelectionMode::Path
                  : render::ViewportRenderer::SelectionMode::Replace;
    std::size_t selection_count = 0;
    if (request.type == ui::ViewportSelectionRequest::Type::Click) {
        selection_count = viewport_renderer_.selectAt(
            request.normalized_x,
            request.normalized_y,
            selection_query,
            selection_mode
        );
    } else if (request.type == ui::ViewportSelectionRequest::Type::Box) {
        selection_count = viewport_renderer_.selectInRect(
            request.normalized_min_x,
            request.normalized_min_y,
            request.normalized_max_x,
            request.normalized_max_y,
            selection_query,
            selection_mode
        );
    }

    appendLog("SELECTION", "Selected (" + std::to_string(selection_count) + ") entities");
}

void EditorApplication::handleInvertSelectionRequest() {
    if (!active_document_.has_value()) {
        return;
    }

    const ui::SelectionFilters& selection_filters = editor_ui_.selectionFilters();
    const render::ViewportRenderer::SelectionQuery selection_query{
        .edges = selection_filters.edges,
        .faces = selection_filters.faces,
        .points = selection_filters.points,
    };
    const std::size_t selection_count = viewport_renderer_.invertSelection(selection_query);
    appendLog("SELECTION", "Selected (" + std::to_string(selection_count) + ") entities");
}

}  // namespace meshtools::app
