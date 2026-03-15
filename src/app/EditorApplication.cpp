#include "meshtools/app/EditorApplication.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>

#include "meshtools/io/MeshImporter.h"
#include "meshtools/io/ProjectArchive.h"
#include "meshtools/mesh/MeshOperations/CreateFace.h"
#include "meshtools/mesh/MeshOperations/DeleteSelection.h"
#include "meshtools/mesh/MeshOperations/Normals.h"
#include "meshtools/mesh/MeshOperations/ProjectEdge.h"
#include "meshtools/platform/FileDialog.h"
#include "meshtools/platform/NativeMenu.h"

namespace meshtools::app {

namespace {

template <typename Func>
float measureMilliseconds(Func&& func) {
    const auto start = std::chrono::steady_clock::now();
    std::forward<Func>(func)();
    return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
}

std::string lowercaseExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

std::string trim(std::string value) {
    const auto is_space = [](unsigned char character) {
        return std::isspace(character) != 0;
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&is_space](char character) {
                    return !is_space(static_cast<unsigned char>(character));
                }));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), [&is_space](char character) {
            return !is_space(static_cast<unsigned char>(character));
        }).base(),
        value.end()
    );
    return value;
}

void sortAndUnique(std::vector<std::uint32_t>& indices) {
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
}

void normalizeEntitySelection(mesh::EntitySelection& selection) {
    sortAndUnique(selection.edge_indices);
    sortAndUnique(selection.face_indices);
    sortAndUnique(selection.point_indices);
}

void appendUniqueIndices(std::vector<std::uint32_t>& target, const std::vector<std::uint32_t>& source) {
    target.insert(target.end(), source.begin(), source.end());
    sortAndUnique(target);
}

bool selectionsEqual(const mesh::EntitySelection& left, const mesh::EntitySelection& right) {
    return left.edge_indices == right.edge_indices &&
           left.face_indices == right.face_indices &&
           left.point_indices == right.point_indices;
}

bool modifyAvailabilityCacheMatches(
    const EditorApplication::ModifyAvailabilityCache& cache,
    const mesh::MeshDocument& document,
    const mesh::EntitySelection& selection
) {
    return cache.valid &&
           cache.source_path == document.source_path &&
           cache.mesh_revision == document.mesh_revision &&
           cache.vertex_count == document.positions.size() &&
           cache.triangle_count == document.triangles.size() &&
           cache.explicit_edge_count == document.explicit_edges.size() &&
           selectionsEqual(cache.selection, selection);
}

bool documentTopologyCacheMatches(
    const EditorApplication::DocumentTopologyCache& cache,
    const mesh::MeshDocument& document
) {
    return cache.valid &&
           cache.source_path == document.source_path &&
           cache.mesh_revision == document.mesh_revision &&
           cache.vertex_count == document.positions.size() &&
           cache.triangle_count == document.triangles.size() &&
           cache.explicit_edge_count == document.explicit_edges.size();
}

template <typename State>
void setAsyncProgress(const std::shared_ptr<State>& state, float progress, bool determinate_progress) {
    if (!state) {
        return;
    }

    const std::scoped_lock lock(state->mutex);
    state->progress = std::clamp(progress, 0.0F, 1.0F);
    state->determinate_progress = determinate_progress;
}

render::ViewportRenderer::ExpandSelectionParams makeExpandSelectionParams(
    const ui::EditorUiActions::ExpandSelectionConfig& config
) {
    render::ViewportRenderer::ExpandSelectionMethod method = render::ViewportRenderer::ExpandSelectionMethod::Coplanar;
    switch (config.method) {
        case ui::ExpandSelectionMethod::Coplanar:
            method = render::ViewportRenderer::ExpandSelectionMethod::Coplanar;
            break;
        case ui::ExpandSelectionMethod::Adjacent:
            method = render::ViewportRenderer::ExpandSelectionMethod::Adjacent;
            break;
        case ui::ExpandSelectionMethod::IntersectingNormals:
            method = render::ViewportRenderer::ExpandSelectionMethod::IntersectingNormals;
            break;
    }

    return render::ViewportRenderer::ExpandSelectionParams{
        .method = method,
        .coplanar_include_parallel = config.coplanar_include_parallel,
        .coplanar_select_adjacent_only = config.coplanar_select_adjacent_only,
        .coplanar_tolerance_percent = config.coplanar_tolerance_percent,
        .adjacent_max_angle_degrees = config.adjacent_max_angle_degrees,
        .intersecting_include_inverse_normals = config.intersecting_include_inverse_normals,
        .intersecting_tolerance = config.intersecting_tolerance,
        .intersecting_allow_linear_intersection = config.intersecting_allow_linear_intersection,
    };
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
        pollPendingDocumentLoad();
        pollPendingMeshOperation();
        pollPendingHistoryRestore();
        pollPendingDocumentTopologyPrecompute();

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
                .theme_colors = editor_ui_.viewportThemeColors(),
            }
        );
        if (pending_renderer_selection_.has_value()) {
            viewport_renderer_.setSelection(*pending_renderer_selection_);
            pending_renderer_selection_.reset();
        }
        editor_ui_.setViewportTexture(viewport_renderer_.textureId());

        const mesh::EntitySelection current_selection = viewport_renderer_.currentSelection();
        mesh::ModifyDeleteAvailability modify_delete_availability;
        mesh::ModifyCreateFaceAvailability modify_create_face_availability;
        mesh::ModifyProjectAvailability modify_project_availability;
        float modify_delete_availability_ms = 0.0F;
        float modify_create_face_availability_ms = 0.0F;
        float modify_project_availability_ms = 0.0F;
        if (active_document_) {
            const bool topology_cache_ready = documentTopologyCacheMatches(document_topology_cache_, active_document_.value());
            if (modifyAvailabilityCacheMatches(modify_availability_cache_, active_document_.value(), current_selection)) {
                modify_delete_availability = modify_availability_cache_.modify_delete_availability;
                modify_create_face_availability = modify_availability_cache_.modify_create_face_availability;
                modify_project_availability = modify_availability_cache_.modify_project_availability;
            } else if (topology_cache_ready) {
                modify_delete_availability_ms = measureMilliseconds([&]() {
                    modify_delete_availability = mesh::computeModifyDeleteAvailability(
                        active_document_.value(),
                        document_topology_cache_.topology,
                        current_selection
                    );
                });
                modify_create_face_availability_ms = measureMilliseconds([&]() {
                    modify_create_face_availability = mesh::computeModifyCreateFaceAvailability(
                        active_document_.value(),
                        document_topology_cache_.topology,
                        current_selection
                    );
                });
                modify_project_availability_ms = measureMilliseconds([&]() {
                    modify_project_availability =
                        mesh::computeModifyProjectAvailability(active_document_.value(), current_selection);
                });
                modify_availability_cache_ = ModifyAvailabilityCache{
                    .valid = true,
                    .source_path = active_document_->source_path,
                    .mesh_revision = active_document_->mesh_revision,
                    .vertex_count = active_document_->positions.size(),
                    .triangle_count = active_document_->triangles.size(),
                    .explicit_edge_count = active_document_->explicit_edges.size(),
                    .selection = current_selection,
                    .modify_delete_availability = modify_delete_availability,
                    .modify_create_face_availability = modify_create_face_availability,
                    .modify_project_availability = modify_project_availability,
                };
            } else {
                modify_availability_cache_ = ModifyAvailabilityCache{};
            }
        } else {
            modify_availability_cache_ = ModifyAvailabilityCache{};
            document_topology_cache_ = DocumentTopologyCache{};
        }

        const ImVec4& clear_color = editor_ui_.clearColor();
        window_.beginFrame(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        editor_ui_.beginFrame();
        const DocumentHistoryUiState history_ui_state = history_controller_.buildUiState();

        std::vector<ui::EditorUiState::TimedTask> current_frame_performance_tasks;
        current_frame_performance_tasks.reserve(14);
        const render::ViewportRenderer::FrameTiming& render_timing = viewport_renderer_.lastFrameTiming();
        current_frame_performance_tasks.push_back(ui::EditorUiState::TimedTask{
            .label = "Viewport render (total)",
            .duration_ms = render_timing.total_render_ms,
        });
        current_frame_performance_tasks.push_back(ui::EditorUiState::TimedTask{
            .label = "Mesh sync/upload",
            .duration_ms = render_timing.mesh_sync_ms,
        });
        current_frame_performance_tasks.push_back(ui::EditorUiState::TimedTask{
            .label = "Shaded triangles pass",
            .duration_ms = render_timing.shaded_pass_ms,
        });
        current_frame_performance_tasks.push_back(ui::EditorUiState::TimedTask{
            .label = "Wireframe pass",
            .duration_ms = render_timing.wireframe_pass_ms,
        });
        current_frame_performance_tasks.push_back(ui::EditorUiState::TimedTask{
            .label = "Point pass",
            .duration_ms = render_timing.point_pass_ms,
        });
        current_frame_performance_tasks.push_back(ui::EditorUiState::TimedTask{
            .label = "Highlight overlays",
            .duration_ms = render_timing.highlight_pass_ms,
        });
        current_frame_performance_tasks.push_back(ui::EditorUiState::TimedTask{
            .label = "Axes overlay",
            .duration_ms = render_timing.axis_pass_ms,
        });
        current_frame_performance_tasks.push_back(ui::EditorUiState::TimedTask{
            .label = "Framebuffer resize/setup",
            .duration_ms = render_timing.framebuffer_setup_ms,
        });
        current_frame_performance_tasks.push_back(ui::EditorUiState::TimedTask{
            .label = "Modify Delete availability",
            .duration_ms = modify_delete_availability_ms,
        });
        current_frame_performance_tasks.push_back(ui::EditorUiState::TimedTask{
            .label = "Modify Create Face availability",
            .duration_ms = modify_create_face_availability_ms,
        });
        current_frame_performance_tasks.push_back(ui::EditorUiState::TimedTask{
            .label = "Modify Project availability",
            .duration_ms = modify_project_availability_ms,
        });

        bool file_load_dialog_determinate_progress = false;
        float file_load_dialog_progress = 0.0F;
        if (pending_document_load_.has_value()) {
            const std::scoped_lock lock(pending_document_load_->state->mutex);
            file_load_dialog_determinate_progress = pending_document_load_->state->determinate_progress;
            file_load_dialog_progress = pending_document_load_->state->progress;
        } else if (pending_mesh_operation_.has_value()) {
            const std::scoped_lock lock(pending_mesh_operation_->state->mutex);
            file_load_dialog_determinate_progress = pending_mesh_operation_->state->determinate_progress;
            file_load_dialog_progress = pending_mesh_operation_->state->progress;
        } else if (pending_history_restore_.has_value()) {
            const std::scoped_lock lock(pending_history_restore_->state->mutex);
            file_load_dialog_determinate_progress = pending_history_restore_->state->determinate_progress;
            file_load_dialog_progress = pending_history_restore_->state->progress;
        } else if (pending_document_topology_precompute_.has_value()) {
            const std::scoped_lock lock(pending_document_topology_precompute_->state->mutex);
            file_load_dialog_determinate_progress = pending_document_topology_precompute_->state->determinate_progress;
            file_load_dialog_progress = pending_document_topology_precompute_->state->progress;
        }

        const ui::EditorUiState ui_state{
            .modify_delete_availability = modify_delete_availability,
            .modify_create_face_availability = modify_create_face_availability,
            .modify_project_availability = modify_project_availability,
            .current_selection = current_selection,
            .active_document = active_document_ ? &active_document_.value() : nullptr,
            .entity_sets = active_document_ ? std::span<const mesh::EntitySet>(active_document_->entity_sets) : std::span<const mesh::EntitySet>{},
            .selected_entity_set_index = selected_entity_set_index_,
            .history_entries = history_ui_state.entries,
            .active_history_branch = history_ui_state.active_branch,
            .active_history_branch_position = history_ui_state.active_branch_position,
            .can_undo = history_ui_state.can_undo,
            .can_redo = history_ui_state.can_redo,
            .show_performance_tasks = editor_ui_.graphicsQualitySettings().debug_performance,
            .log_messages = logger_.messages(),
            .performance_tasks = frame_performance_tasks_,
            .camera_yaw = viewport_renderer_.camera().yaw,
            .camera_pitch = viewport_renderer_.camera().pitch,
            .selection_summary = ui::EditorUiState::SelectionSummary{
                .edge_count = viewport_renderer_.selectionSummary().edge_count,
                .face_count = viewport_renderer_.selectionSummary().face_count,
                .point_count = viewport_renderer_.selectionSummary().point_count,
            },
            .expand_selection_feedback = ui::EditorUiState::ExpandSelectionFeedback{
                .available = expand_selection_feedback_.available,
                .linear_intersection_enabled = expand_selection_feedback_.linear_intersection_enabled,
                .preview_total_count = expand_selection_feedback_.preview_total_count,
                .preview_face_count = expand_selection_feedback_.preview_face_count,
                .preview_added_face_count = expand_selection_feedback_.preview_added_face_count,
                .unavailable_reasons = expand_selection_feedback_reasons_,
            },
            .select_similar_feedback = ui::EditorUiState::SelectSimilarFeedback{
                .available = select_similar_feedback_.available,
                .match_count = select_similar_feedback_.match_count,
                .preview_edge_count = select_similar_feedback_.preview_edge_count,
                .unavailable_reasons = select_similar_feedback_reasons_,
            },
            .file_load_dialog = ui::EditorUiState::FileLoadDialog{
                .visible =
                    pending_document_load_.has_value() ||
                    pending_mesh_operation_.has_value() ||
                    pending_history_restore_.has_value() ||
                    pending_document_topology_precompute_.has_value(),
                .show_progress_bar =
                    pending_document_load_.has_value() ||
                    pending_mesh_operation_.has_value() ||
                    pending_history_restore_.has_value() ||
                    pending_document_topology_precompute_.has_value(),
                .determinate_progress = file_load_dialog_determinate_progress,
                .progress = file_load_dialog_progress,
                .title =
                    pending_document_load_.has_value()
                        ? pending_document_load_->kind == PendingDocumentLoad::Kind::Project
                              ? "Opening project"
                              : "Importing mesh"
                        : pending_mesh_operation_.has_value()
                            ? pending_mesh_operation_->title
                        : pending_history_restore_.has_value()
                            ? pending_history_restore_->title
                        : pending_document_topology_precompute_.has_value()
                            ? pending_document_topology_precompute_->title
                            : "",
                .message =
                    pending_document_load_.has_value()
                        ? "Loading " + pending_document_load_->path.filename().string() + '.'
                        : pending_mesh_operation_.has_value()
                            ? pending_mesh_operation_->message
                        : pending_history_restore_.has_value()
                            ? pending_history_restore_->message
                        : pending_document_topology_precompute_.has_value()
                            ? pending_document_topology_precompute_->message
                        : "",
            },
        };
        float editor_ui_draw_ms = 0.0F;
        ui::EditorUiActions actions;
        editor_ui_draw_ms = measureMilliseconds([&]() {
            actions = editor_ui_.draw(ui_state);
        });
        float imgui_render_ms = measureMilliseconds([&]() {
            editor_ui_.endFrame(window_.nativeHandle());
        });
        float swap_buffers_ms = measureMilliseconds([&]() {
            window_.swapBuffers();
        });
        current_frame_performance_tasks.push_back(ui::EditorUiState::TimedTask{
            .label = "Editor UI build",
            .duration_ms = editor_ui_draw_ms,
        });
        current_frame_performance_tasks.push_back(ui::EditorUiState::TimedTask{
            .label = "ImGui render",
            .duration_ms = imgui_render_ms,
        });
        current_frame_performance_tasks.push_back(ui::EditorUiState::TimedTask{
            .label = "Present / swap buffers",
            .duration_ms = swap_buffers_ms,
        });
        std::sort(
            current_frame_performance_tasks.begin(),
            current_frame_performance_tasks.end(),
            [](const ui::EditorUiState::TimedTask& left, const ui::EditorUiState::TimedTask& right) {
                return left.duration_ms > right.duration_ms;
            }
        );
        frame_performance_tasks_ = std::move(current_frame_performance_tasks);

        if (pending_document_load_.has_value() ||
            pending_mesh_operation_.has_value() ||
            pending_history_restore_.has_value() ||
            pending_document_topology_precompute_.has_value()) {
            if (pending_mesh_operation_.has_value() && !pending_mesh_operation_->started) {
                startPendingMeshOperation();
            }
            if (pending_history_restore_.has_value() && !pending_history_restore_->started) {
                startPendingHistoryRestore();
            }
            if (pending_document_topology_precompute_.has_value() && !pending_document_topology_precompute_->started) {
                startPendingDocumentTopologyPrecompute();
            }
            pending_viewport_camera_input_ = ui::ViewportCameraInput{};
            if (actions.request_exit) {
                window_.requestClose();
            }
            continue;
        }

        pending_viewport_camera_input_ = actions.viewport_camera;
        for (const ui::EditorUiLogEvent& event_log : actions.event_logs) {
            appendLog(event_log.origin, event_log.message);
        }
        if (menu_actions.undo || actions.request_undo) {
            undoDocumentHistory();
        }
        if (menu_actions.redo || actions.request_redo) {
            redoDocumentHistory();
        }
        if (actions.request_history_node_id.has_value()) {
            jumpToDocumentHistoryNode(*actions.request_history_node_id);
        }
        handleViewportSelectionRequest(actions.viewport_selection);
        if (actions.request_select_edge_loop) {
            handleSelectEdgeLoopRequest();
        }
        if (actions.request_invert_selection) {
            handleInvertSelectionRequest();
        }
        handleSelectSimilarActions(actions);
        handleExpandSelectionActions(actions);
        handleProjectActions(actions);
        handleModifyCreateFaceRequest(actions);
        handleModifyProjectRequest(actions);
        handleModifyDeleteRequest(actions);
        handleEntitySetActions(actions);

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
    logger_.append(std::move(origin), std::move(message));
}

void EditorApplication::openDocument() {
    if (pending_document_load_.has_value() ||
        pending_mesh_operation_.has_value() ||
        pending_history_restore_.has_value() ||
        pending_document_topology_precompute_.has_value()) {
        appendLog("PROJECT", "Open skipped because another file is still loading.");
        return;
    }

    const std::optional<std::filesystem::path> selected_path = platform::openDocumentFileDialog();
    if (!selected_path.has_value()) {
        appendLog("PROJECT", "Open canceled.");
        return;
    }

    openPath(*selected_path);
}

void EditorApplication::openPath(const std::filesystem::path& path) {
    if (pending_document_load_.has_value() ||
        pending_mesh_operation_.has_value() ||
        pending_history_restore_.has_value() ||
        pending_document_topology_precompute_.has_value()) {
        appendLog("PROJECT", "Open skipped because another file is still loading.");
        return;
    }

    beginDocumentLoad(path);
}

void EditorApplication::saveProject() {
    if (pending_document_load_.has_value() ||
        pending_mesh_operation_.has_value() ||
        pending_history_restore_.has_value() ||
        pending_document_topology_precompute_.has_value()) {
        appendLog("PROJECT", "Save skipped because the document is busy.");
        return;
    }

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
            .log_messages = logger_.messages(),
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
    if (pending_document_load_.has_value() ||
        pending_mesh_operation_.has_value() ||
        pending_history_restore_.has_value() ||
        pending_document_topology_precompute_.has_value()) {
        appendLog("PROJECT", "Save skipped because the document is busy.");
        return;
    }

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
            .log_messages = logger_.messages(),
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

void EditorApplication::beginDocumentLoad(const std::filesystem::path& path) {
    PendingDocumentLoad pending_load;
    pending_load.kind =
        lowercaseExtension(path) == ".mt" ? PendingDocumentLoad::Kind::Project : PendingDocumentLoad::Kind::Mesh;
    pending_load.path = path;
    pending_load.show_dialog = !active_document_.has_value();
    pending_load.state = std::make_shared<AsyncDocumentLoadState>();

    const PendingDocumentLoad::Kind kind = pending_load.kind;
    const std::filesystem::path load_path = pending_load.path;
    const std::shared_ptr<AsyncDocumentLoadState> state = pending_load.state;
    setAsyncProgress(state, 0.0F, false);
    pending_load.worker = std::jthread([state, kind, load_path]() {
        DocumentLoadOutcome outcome;
        if (kind == PendingDocumentLoad::Kind::Project) {
            io::ProjectArchiveLoadResult result = io::loadProjectArchive(
                load_path,
                [state](float progress, std::string_view) {
                    setAsyncProgress(state, progress, true);
                }
            );
            outcome.document = std::move(result.document);
            outcome.log_messages = std::move(result.log_messages);
            outcome.error_message = std::move(result.error_message);
        } else {
            io::MeshImportResult result = io::importMeshFromFile(
                load_path,
                [state](float progress, std::string_view) {
                    setAsyncProgress(state, progress, true);
                }
            );
            outcome.document = std::move(result.document);
            outcome.error_message = std::move(result.error_message);
        }

        const std::scoped_lock lock(state->mutex);
        state->progress = 1.0F;
        state->outcome = std::move(outcome);
        state->completed = true;
    });
    pending_document_load_ = std::move(pending_load);
}

void EditorApplication::pollPendingDocumentLoad() {
    if (!pending_document_load_.has_value()) {
        return;
    }

    std::optional<DocumentLoadOutcome> outcome;
    {
        const std::scoped_lock lock(pending_document_load_->state->mutex);
        if (!pending_document_load_->state->completed) {
            return;
        }

        outcome = std::move(pending_document_load_->state->outcome);
    }

    PendingDocumentLoad completed_load = std::move(*pending_document_load_);
    pending_document_load_.reset();
    if (completed_load.kind == PendingDocumentLoad::Kind::Project) {
        applyLoadedProjectDocument(completed_load.path, std::move(*outcome));
        return;
    }

    applyLoadedMeshDocument(std::move(*outcome));
}

void EditorApplication::beginModifyCreateFaceOperation() {
    if (!active_document_.has_value()) {
        return;
    }

    pending_mesh_operation_ = PendingMeshOperation{
        .kind = PendingMeshOperation::Kind::CreateFace,
        .title = "Creating faces",
        .message = "Applying face creation to " + active_document_->displayName() + '.',
        .started = false,
        .document_snapshot = active_document_.value(),
        .selection = viewport_renderer_.currentSelection(),
        .state = std::make_shared<AsyncMeshOperationState>(),
    };
}

void EditorApplication::beginModifyProjectOperation(const mesh::ModifyProjectOptions& options) {
    if (!active_document_.has_value()) {
        return;
    }

    pending_mesh_operation_ = PendingMeshOperation{
        .kind = PendingMeshOperation::Kind::Project,
        .title = "Projecting edge",
        .message = "Applying projected edge changes to " + active_document_->displayName() + '.',
        .started = false,
        .document_snapshot = active_document_.value(),
        .selection = viewport_renderer_.currentSelection(),
        .project_options = options,
        .state = std::make_shared<AsyncMeshOperationState>(),
    };
}

void EditorApplication::beginModifyDeleteOperation(const mesh::ModifyDeleteOptions& options) {
    if (!active_document_.has_value()) {
        return;
    }

    pending_mesh_operation_ = PendingMeshOperation{
        .kind = PendingMeshOperation::Kind::Delete,
        .title = "Deleting geometry",
        .message = "Applying delete operation to " + active_document_->displayName() + '.',
        .started = false,
        .document_snapshot = active_document_.value(),
        .selection = viewport_renderer_.currentSelection(),
        .delete_options = options,
        .state = std::make_shared<AsyncMeshOperationState>(),
    };
}

void EditorApplication::startPendingMeshOperation() {
    if (!pending_mesh_operation_.has_value() ||
        pending_mesh_operation_->started) {
        return;
    }

    pending_mesh_operation_->started = true;
    const PendingMeshOperation::Kind kind = pending_mesh_operation_->kind;
    const std::shared_ptr<AsyncMeshOperationState> state = pending_mesh_operation_->state;
    setAsyncProgress(state, 0.0F, false);
    mesh::MeshDocument document_snapshot = std::move(pending_mesh_operation_->document_snapshot);
    mesh::EntitySelection selection = pending_mesh_operation_->selection;
    const mesh::ModifyProjectOptions project_options = pending_mesh_operation_->project_options;
    const mesh::ModifyDeleteOptions delete_options = pending_mesh_operation_->delete_options;
    pending_mesh_operation_->worker = std::jthread(
        [state, kind, document_snapshot = std::move(document_snapshot), selection = std::move(selection), project_options, delete_options]() mutable {
            MeshOperationOutcome outcome;

            switch (kind) {
                case PendingMeshOperation::Kind::CreateFace: {
                    const mesh::ModifyCreateFaceResult result =
                        mesh::applyModifyCreateFace(&document_snapshot, selection);
                    if (!result.changed) {
                        outcome.skipped_log_message =
                            "Create face skipped because the selection could not form a face.";
                        break;
                    }

                    outcome.changed = true;
                    outcome.document_state = makeEditableDocumentState(document_snapshot);
                    outcome.selection_after = mesh::EntitySelection{
                        .edge_indices = {},
                        .face_indices = result.created_face_indices,
                        .point_indices = {},
                    };
                    outcome.history_label = "Created " + std::to_string(result.created_face_count) + " faces";
                    outcome.success_log_message = "Created " + std::to_string(result.created_face_count) + " faces.";
                    outcome.cache_title = "Updating mesh caches";
                    outcome.cache_message = "Recomputing topology caches after face creation.";
                    break;
                }
                case PendingMeshOperation::Kind::Project: {
                    const mesh::ModifyProjectResult result =
                        mesh::applyModifyProject(&document_snapshot, project_options);
                    if (!result.changed) {
                        outcome.skipped_log_message =
                            "Project skipped because the requested line could not be created.";
                        break;
                    }

                    outcome.changed = true;
                    outcome.document_state = makeEditableDocumentState(document_snapshot);
                    outcome.selection_after = mesh::EntitySelection{
                        .edge_indices =
                            result.created_edge_index.has_value()
                                ? std::vector<std::uint32_t>{*result.created_edge_index}
                                : std::vector<std::uint32_t>{},
                        .face_indices = {},
                        .point_indices = {},
                    };
                    outcome.history_label = "Projected edge";
                    outcome.success_log_message = "Projected 2 faces and created 1 edge.";
                    outcome.cache_title = "Updating mesh caches";
                    outcome.cache_message = "Recomputing topology caches after projected edge creation.";
                    break;
                }
                case PendingMeshOperation::Kind::Delete: {
                    const mesh::ModifyDeleteResult result =
                        mesh::applyModifyDelete(&document_snapshot, selection, delete_options);
                    if (!result.changed) {
                        outcome.skipped_log_message =
                            "Delete skipped because nothing applicable was selected.";
                        break;
                    }

                    outcome.changed = true;
                    outcome.document_state = makeEditableDocumentState(document_snapshot);
                    outcome.selection_after = mesh::EntitySelection{};
                    outcome.history_label = "Deleted geometry";
                    outcome.success_log_message =
                        "Deleted " + std::to_string(result.deleted_face_count) + " faces and " +
                        std::to_string(result.deleted_edge_count) + " edges and " +
                        std::to_string(result.deleted_point_count) + " points.";
                    outcome.cache_title = "Updating mesh caches";
                    outcome.cache_message = "Recomputing topology caches after deleting geometry.";
                    break;
                }
            }

            const std::scoped_lock lock(state->mutex);
            state->progress = 1.0F;
            state->outcome = std::move(outcome);
            state->completed = true;
        }
    );
}

void EditorApplication::pollPendingMeshOperation() {
    if (!pending_mesh_operation_.has_value() || !pending_mesh_operation_->started) {
        return;
    }

    std::optional<MeshOperationOutcome> outcome;
    {
        const std::scoped_lock lock(pending_mesh_operation_->state->mutex);
        if (!pending_mesh_operation_->state->completed) {
            return;
        }

        outcome = std::move(pending_mesh_operation_->state->outcome);
    }

    pending_mesh_operation_.reset();
    if (!outcome.has_value()) {
        return;
    }

    if (!outcome->changed) {
        if (!outcome->skipped_log_message.empty()) {
            appendLog("MODIFY", outcome->skipped_log_message);
        }
        return;
    }

    if (!active_document_.has_value()) {
        return;
    }

    applyEditableDocumentState(&active_document_.value(), outcome->document_state);
    viewport_renderer_.clearSelection();
    viewport_renderer_.clearExpandSelectionPreview();
    viewport_renderer_.clearSelectSimilarPreview();
    expand_selection_feedback_reasons_.clear();
    expand_selection_feedback_ = {};
    select_similar_feedback_reasons_.clear();
    select_similar_feedback_ = {};
    selected_entity_set_index_ = outcome->selected_entity_set_index;
    if (outcome->selection_after.empty()) {
        pending_renderer_selection_.reset();
    } else {
        pending_renderer_selection_ = outcome->selection_after;
    }
    bumpMeshRevision();
    beginDocumentTopologyPrecompute(outcome->cache_title, outcome->cache_message);
    commitDocumentHistory(
        outcome->history_label,
        outcome->selection_after,
        outcome->selected_entity_set_index
    );
    appendLog("MODIFY", outcome->success_log_message);
}

void EditorApplication::beginHistoryRestoreOperation(DocumentHistoryRestoreTarget target, std::string action) {
    if (!active_document_.has_value()) {
        return;
    }

    PendingHistoryRestore pending_restore;
    pending_restore.target = std::move(target);
    pending_restore.action = std::move(action);
    pending_restore.title = "Restoring history";
    pending_restore.message = pending_restore.action + " " + pending_restore.target.node_label + '.';
    pending_restore.state = std::make_shared<AsyncHistoryRestoreState>();
    pending_history_restore_ = std::move(pending_restore);
}

void EditorApplication::startPendingHistoryRestore() {
    if (!pending_history_restore_.has_value() ||
        pending_history_restore_->started) {
        return;
    }

    pending_history_restore_->started = true;
    const DocumentHistoryRestoreTarget target = pending_history_restore_->target;
    const std::string action = pending_history_restore_->action;
    const std::shared_ptr<AsyncHistoryRestoreState> state = pending_history_restore_->state;
    setAsyncProgress(state, 0.0F, false);
    pending_history_restore_->worker = std::jthread(
        [state, target, action]() mutable {
            HistoryRestoreOutcome outcome;
            outcome.valid = true;
            outcome.target = target;
            outcome.action = std::move(action);

            const std::scoped_lock lock(state->mutex);
            state->progress = 1.0F;
            state->outcome = std::move(outcome);
            state->completed = true;
        }
    );
}

void EditorApplication::pollPendingHistoryRestore() {
    if (!pending_history_restore_.has_value() || !pending_history_restore_->started) {
        return;
    }

    std::optional<HistoryRestoreOutcome> outcome;
    {
        const std::scoped_lock lock(pending_history_restore_->state->mutex);
        if (!pending_history_restore_->state->completed) {
            return;
        }

        outcome = std::move(pending_history_restore_->state->outcome);
    }

    pending_history_restore_.reset();
    if (!outcome.has_value() || !outcome->valid || !active_document_.has_value()) {
        return;
    }

    applyEditableDocumentState(&active_document_.value(), outcome->target.snapshot.document_state);
    viewport_renderer_.clearSelection();
    pending_renderer_selection_ = outcome->target.snapshot.selection;
    selected_entity_set_index_ = outcome->target.snapshot.selected_entity_set_index;
    if (selected_entity_set_index_.has_value() && *selected_entity_set_index_ >= active_document_->entity_sets.size()) {
        selected_entity_set_index_.reset();
    }
    viewport_renderer_.clearExpandSelectionPreview();
    viewport_renderer_.clearSelectSimilarPreview();
    expand_selection_feedback_reasons_.clear();
    expand_selection_feedback_ = {};
    select_similar_feedback_reasons_.clear();
    select_similar_feedback_ = {};
    history_controller_.markRestored(outcome->target.node_index);
    next_mesh_revision_id_ = std::max(
        next_mesh_revision_id_,
        outcome->target.snapshot.document_state.mesh_revision + 1U
    );
    beginDocumentTopologyPrecompute(
        "Updating mesh caches",
        "Recomputing topology caches for restored history state."
    );
    appendLog("HISTORY", std::move(outcome->action) + " " + outcome->target.node_label + '.');
}

void EditorApplication::beginDocumentTopologyPrecompute(std::string title, std::string message) {
    if (!active_document_.has_value()) {
        return;
    }

    document_topology_cache_ = DocumentTopologyCache{};
    modify_availability_cache_ = ModifyAvailabilityCache{};

    PendingDocumentTopologyPrecompute pending_precompute;
    pending_precompute.title = std::move(title);
    pending_precompute.message = std::move(message);
    pending_precompute.state = std::make_shared<AsyncDocumentTopologyPrecomputeState>();
    pending_document_topology_precompute_ = std::move(pending_precompute);
}

void EditorApplication::startPendingDocumentTopologyPrecompute() {
    if (!pending_document_topology_precompute_.has_value() ||
        pending_document_topology_precompute_->started ||
        !active_document_.has_value()) {
        return;
    }

    pending_document_topology_precompute_->started = true;
    mesh::MeshDocument document_snapshot = active_document_.value();
    const std::shared_ptr<AsyncDocumentTopologyPrecomputeState> state = pending_document_topology_precompute_->state;
    setAsyncProgress(state, 0.0F, true);
    pending_document_topology_precompute_->worker =
        std::jthread([state, document_snapshot = std::move(document_snapshot)]() mutable {
            DocumentTopologyCache cache;
            cache.valid = true;
            cache.source_path = document_snapshot.source_path;
            cache.mesh_revision = document_snapshot.mesh_revision;
            cache.vertex_count = document_snapshot.positions.size();
            cache.triangle_count = document_snapshot.triangles.size();
            cache.explicit_edge_count = document_snapshot.explicit_edges.size();
            cache.topology = mesh::operations::detail::buildMeshTopology(
                document_snapshot,
                [state](float progress) {
                    setAsyncProgress(state, progress, true);
                }
            );

            const std::scoped_lock lock(state->mutex);
            state->progress = 1.0F;
            state->cache = std::move(cache);
            state->completed = true;
        });
}

void EditorApplication::pollPendingDocumentTopologyPrecompute() {
    if (!pending_document_topology_precompute_.has_value()) {
        return;
    }

    std::optional<DocumentTopologyCache> cache;
    {
        const std::scoped_lock lock(pending_document_topology_precompute_->state->mutex);
        if (!pending_document_topology_precompute_->state->completed) {
            return;
        }

        cache = std::move(pending_document_topology_precompute_->state->cache);
    }

    pending_document_topology_precompute_.reset();
    if (!active_document_.has_value() || !cache.has_value()) {
        return;
    }

    if (!documentTopologyCacheMatches(*cache, active_document_.value())) {
        return;
    }

    document_topology_cache_ = std::move(*cache);
    modify_availability_cache_ = ModifyAvailabilityCache{};
    appendLog(
        "PROJECT",
        "Prepared topology cache for " + active_document_->displayName() +
            " (" + std::to_string(active_document_->triangles.size()) + " triangles)."
    );
}

void EditorApplication::applyLoadedMeshDocument(DocumentLoadOutcome outcome) {
    if (!outcome.document.has_value()) {
        appendLog("IMPORT", "Failed to load mesh: " + outcome.error_message);
        return;
    }

    active_document_ = std::move(outcome.document);
    mesh::ensureRenderableNormals(&active_document_.value());
    active_document_->mesh_revision = next_mesh_revision_id_++;
    viewport_renderer_.clearSelection();
    viewport_renderer_.clearExpandSelectionPreview();
    active_document_->display_name_override.clear();
    active_document_->up_axis = editor_ui_.fileImportSettings().up_axis;
    active_project_path_.clear();
    selected_entity_set_index_.reset();
    pending_renderer_selection_.reset();
    expand_selection_feedback_reasons_.clear();
    expand_selection_feedback_ = {};
    select_similar_feedback_reasons_.clear();
    select_similar_feedback_ = {};
    resetDocumentHistory("Imported " + active_document_->displayName());
    appendLog(
        "IMPORT",
        "Loaded " + active_document_->displayName() +
        " (" + active_document_->formatLabel() + ", " +
        std::to_string(active_document_->positions.size()) + " vertices, " +
        std::to_string(active_document_->triangles.size()) + " triangles)."
    );
    beginDocumentTopologyPrecompute(
        "Preparing mesh caches",
        "Precomputing topology caches for " + active_document_->displayName() + '.'
    );
}

void EditorApplication::applyLoadedProjectDocument(const std::filesystem::path& path, DocumentLoadOutcome outcome) {
    if (!outcome.document.has_value()) {
        appendLog("PROJECT", "Failed to open project: " + outcome.error_message);
        return;
    }

    active_document_ = std::move(outcome.document);
    mesh::ensureRenderableNormals(&active_document_.value());
    active_document_->mesh_revision = next_mesh_revision_id_++;
    viewport_renderer_.clearSelection();
    viewport_renderer_.clearExpandSelectionPreview();
    active_project_path_ = path;
    selected_entity_set_index_.reset();
    pending_renderer_selection_.reset();
    logger_.replace(std::move(outcome.log_messages));
    expand_selection_feedback_reasons_.clear();
    expand_selection_feedback_ = {};
    select_similar_feedback_reasons_.clear();
    select_similar_feedback_ = {};
    resetDocumentHistory("Opened " + active_document_->displayName());
    appendLog(
        "PROJECT",
        "Opened " + active_document_->displayName() +
        " (" + active_document_->formatLabel() + ", " +
        std::to_string(active_document_->positions.size()) + " vertices, " +
        std::to_string(active_document_->triangles.size()) + " triangles)."
    );
    beginDocumentTopologyPrecompute(
        "Preparing mesh caches",
        "Precomputing topology caches for " + active_document_->displayName() + '.'
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

void EditorApplication::handleProjectActions(const ui::EditorUiActions& actions) {
    if (!active_document_.has_value() || !actions.request_set_project_up_axis.has_value()) {
        return;
    }

    if (active_document_->up_axis == *actions.request_set_project_up_axis) {
        return;
    }

    active_document_->up_axis = *actions.request_set_project_up_axis;
    bumpMeshRevision();
    commitDocumentHistory(
        std::string("Up axis -> ") + mesh::upAxisName(active_document_->up_axis),
        viewport_renderer_.currentSelection(),
        selected_entity_set_index_
    );
    appendLog("PROJECT", "Up axis set to " + std::string(mesh::upAxisName(active_document_->up_axis)) + '.');
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

    selected_entity_set_index_.reset();
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
    selected_entity_set_index_.reset();
    appendLog("SELECTION", "Selected (" + std::to_string(selection_count) + ") entities");
}

void EditorApplication::handleSelectEdgeLoopRequest() {
    if (!active_document_.has_value()) {
        return;
    }

    const render::ViewportRenderer::EdgeLoopSelectionResult result = viewport_renderer_.selectEdgeLoop();
    if (!result.available) {
        appendLog(
            "SELECTION",
            result.unavailable_reason.empty()
                ? "Edge loop unavailable."
                : "Edge loop unavailable: " + result.unavailable_reason
        );
        return;
    }

    selected_entity_set_index_.reset();
    appendLog(
        "SELECTION",
        "Selected edge loop " +
            std::to_string(result.selected_candidate_index + 1U) + "/" +
            std::to_string(result.candidate_count) +
            " (" + result.candidate_label + ", " +
            std::to_string(result.selection.edge_indices.size()) + " edges)."
    );
}

void EditorApplication::handleModifyCreateFaceRequest(const ui::EditorUiActions& actions) {
    if (!active_document_.has_value() || !actions.request_modify_create_face) {
        return;
    }

    beginModifyCreateFaceOperation();
}

void EditorApplication::handleModifyProjectRequest(const ui::EditorUiActions& actions) {
    if (!active_document_.has_value() || !actions.request_modify_project.has_value()) {
        return;
    }

    appendLog(
        "MODIFY",
        "Modify Project requested from faces " +
            std::to_string(actions.request_modify_project->source_face_indices[0]) + " and " +
            std::to_string(actions.request_modify_project->source_face_indices[1]) + "."
    );
    const mesh::ModifyProjectOptions options{
        .source_face_indices = actions.request_modify_project->source_face_indices,
        .infinite_length = actions.request_modify_project->infinite_length,
        .start_target = actions.request_modify_project->start_target,
        .end_target = actions.request_modify_project->end_target,
    };
    beginModifyProjectOperation(options);
}

void EditorApplication::handleModifyDeleteRequest(const ui::EditorUiActions& actions) {
    if (!active_document_.has_value() || !actions.request_modify_delete.has_value()) {
        return;
    }

    const mesh::ModifyDeleteOptions options{
        .faces = actions.request_modify_delete->faces,
        .inside_edges = actions.request_modify_delete->inside_edges,
        .outside_edges = actions.request_modify_delete->outside_edges,
        .points = actions.request_modify_delete->points,
    };
    beginModifyDeleteOperation(options);
}

void EditorApplication::handleEntitySetActions(const ui::EditorUiActions& actions) {
    if (!active_document_.has_value()) {
        return;
    }

    if (actions.request_select_document_scene_item) {
        selected_entity_set_index_.reset();
    }

    if (actions.request_select_entity_set_index.has_value()) {
        selectEntitySet(*actions.request_select_entity_set_index);
    }

    if (actions.request_create_entity_set_from_selection) {
        createEntitySetFromCurrentSelection();
    }

    if (actions.request_add_selection_to_existing_entity_set_index.has_value()) {
        addCurrentSelectionToEntitySet(*actions.request_add_selection_to_existing_entity_set_index);
    }

    if (actions.request_rename_entity_set.has_value()) {
        const std::size_t index = actions.request_rename_entity_set->index;
        if (index < active_document_->entity_sets.size()) {
            const std::string trimmed_name = trim(actions.request_rename_entity_set->name);
            if (trimmed_name.empty()) {
                appendLog("ENTITYSET", "Rename skipped because the entity set name was empty.");
                return;
            }
            if (active_document_->entity_sets[index].name == trimmed_name) {
                appendLog("ENTITYSET", "Rename skipped because the entity set name was unchanged.");
                return;
            }

            active_document_->entity_sets[index].name = trimmed_name;
            commitDocumentHistory(
                "Renamed entity set",
                viewport_renderer_.currentSelection(),
                selected_entity_set_index_
            );
            appendLog("ENTITYSET", "Renamed entity set to " + trimmed_name + '.');
        }
    }
}

void EditorApplication::handleExpandSelectionActions(const ui::EditorUiActions& actions) {
    if (!active_document_.has_value()) {
        expand_selection_feedback_reasons_.clear();
        expand_selection_feedback_ = {};
        viewport_renderer_.clearExpandSelectionPreview();
        return;
    }

    if (!actions.request_expand_selection.has_value()) {
        if (!actions.expand_selection_dialog_open) {
            expand_selection_feedback_reasons_.clear();
            expand_selection_feedback_ = {};
            viewport_renderer_.clearExpandSelectionPreview();
        }
        return;
    }

    const render::ViewportRenderer::ExpandSelectionResult result =
        viewport_renderer_.evaluateExpandSelection(makeExpandSelectionParams(actions.request_expand_selection->config));

    expand_selection_feedback_reasons_ = result.unavailable_reasons;
    expand_selection_feedback_ = ui::EditorUiState::ExpandSelectionFeedback{
        .available = result.available,
        .linear_intersection_enabled = result.linear_intersection_enabled,
        .preview_total_count = result.selection.totalCount(),
        .preview_face_count = result.selection.face_indices.size(),
        .preview_added_face_count = result.preview_face_indices.size(),
        .unavailable_reasons = expand_selection_feedback_reasons_,
    };
    viewport_renderer_.setExpandSelectionPreview(result.preview_face_indices);

    if (actions.request_expand_selection->intent == ui::EditorUiActions::ExpandSelectionRequest::Intent::Preview) {
        return;
    }

    if (!result.available) {
        if (!result.unavailable_reasons.empty()) {
            appendLog("SELECTION", "Expand selection unavailable: " + result.unavailable_reasons.front());
        } else {
            appendLog("SELECTION", "Expand selection unavailable.");
        }
        return;
    }

    switch (actions.request_expand_selection->intent) {
        case ui::EditorUiActions::ExpandSelectionRequest::Intent::Preview:
            return;
        case ui::EditorUiActions::ExpandSelectionRequest::Intent::Select:
            viewport_renderer_.setSelection(result.selection);
            selected_entity_set_index_.reset();
            appendLog("SELECTION", "Expanded selection to (" + std::to_string(result.selection.totalCount()) + ") entities.");
            break;
        case ui::EditorUiActions::ExpandSelectionRequest::Intent::CreateEntitySet:
            createEntitySetFromSelection(result.selection);
            break;
        case ui::EditorUiActions::ExpandSelectionRequest::Intent::AddToExistingEntitySet:
            if (actions.request_expand_selection->entity_set_index.has_value()) {
                addSelectionToEntitySet(*actions.request_expand_selection->entity_set_index, result.selection);
            }
            break;
    }

    viewport_renderer_.clearExpandSelectionPreview();
    expand_selection_feedback_reasons_.clear();
    expand_selection_feedback_ = {};
}

void EditorApplication::handleSelectSimilarActions(const ui::EditorUiActions& actions) {
    if (!active_document_.has_value()) {
        select_similar_feedback_reasons_.clear();
        select_similar_feedback_ = {};
        viewport_renderer_.clearSelectSimilarPreview();
        return;
    }

    if (!actions.request_select_similar.has_value()) {
        if (!actions.select_similar_dialog_open) {
            select_similar_feedback_reasons_.clear();
            select_similar_feedback_ = {};
            viewport_renderer_.clearSelectSimilarPreview();
        }
        return;
    }

    const render::SelectSimilarResult result =
        viewport_renderer_.evaluateSelectSimilar(actions.request_select_similar->config);
    select_similar_feedback_reasons_ = result.unavailable_reasons;
    select_similar_feedback_ = ui::EditorUiState::SelectSimilarFeedback{
        .available = result.available,
        .match_count = result.match_count,
        .preview_edge_count = result.preview_edge_indices.size(),
        .unavailable_reasons = select_similar_feedback_reasons_,
    };
    viewport_renderer_.setSelectSimilarPreview(result.preview_edge_indices);

    if (actions.request_select_similar->intent == ui::EditorUiActions::SelectSimilarRequest::Intent::Preview) {
        return;
    }

    if (!result.available) {
        appendLog(
            "SELECTION",
            result.unavailable_reasons.empty()
                ? "Select Similar unavailable."
                : "Select Similar unavailable: " + result.unavailable_reasons.front()
        );
        return;
    }

    viewport_renderer_.setSelection(result.selection);
    selected_entity_set_index_.reset();
    viewport_renderer_.clearSelectSimilarPreview();
    select_similar_feedback_reasons_.clear();
    select_similar_feedback_ = {};
    appendLog(
        "SELECTION",
        "Added " + std::to_string(result.preview_edge_indices.size()) +
            " similar edges from " + std::to_string(result.match_count) + " matching groups."
    );
}

void EditorApplication::createEntitySetFromCurrentSelection() {
    if (!active_document_.has_value()) {
        return;
    }

    createEntitySetFromSelection(viewport_renderer_.currentSelection());
}

void EditorApplication::createEntitySetFromSelection(mesh::EntitySelection selection) {
    if (!active_document_.has_value()) {
        return;
    }

    normalizeEntitySelection(selection);
    if (selection.empty()) {
        appendLog("ENTITYSET", "Create skipped because nothing is selected.");
        return;
    }

    mesh::EntitySet entity_set{
        .name = makeDefaultEntitySetName(),
        .members = std::move(selection),
    };
    active_document_->entity_sets.push_back(entity_set);
    selected_entity_set_index_ = active_document_->entity_sets.size() - 1U;
    commitDocumentHistory(
        "Created " + entity_set.name,
        viewport_renderer_.currentSelection(),
        selected_entity_set_index_
    );
    appendLog(
        "ENTITYSET",
        "Created " + entity_set.name + " (" + std::to_string(entity_set.members.totalCount()) + " entities)."
    );
}

void EditorApplication::addCurrentSelectionToEntitySet(std::size_t index) {
    if (!active_document_.has_value() || index >= active_document_->entity_sets.size()) {
        return;
    }

    addSelectionToEntitySet(index, viewport_renderer_.currentSelection());
}

void EditorApplication::addSelectionToEntitySet(std::size_t index, mesh::EntitySelection selection) {
    if (!active_document_.has_value() || index >= active_document_->entity_sets.size()) {
        return;
    }

    normalizeEntitySelection(selection);
    if (selection.empty()) {
        appendLog("ENTITYSET", "Add skipped because nothing is selected.");
        return;
    }

    mesh::EntitySet& entity_set = active_document_->entity_sets[index];
    const mesh::EntitySelection previous_members = entity_set.members;
    appendUniqueIndices(entity_set.members.edge_indices, selection.edge_indices);
    appendUniqueIndices(entity_set.members.face_indices, selection.face_indices);
    appendUniqueIndices(entity_set.members.point_indices, selection.point_indices);
    if (entity_set.members.edge_indices == previous_members.edge_indices &&
        entity_set.members.face_indices == previous_members.face_indices &&
        entity_set.members.point_indices == previous_members.point_indices) {
        appendLog("ENTITYSET", "Add skipped because the selection was already in " + entity_set.name + '.');
        return;
    }
    selected_entity_set_index_ = index;
    commitDocumentHistory(
        "Updated " + entity_set.name,
        viewport_renderer_.currentSelection(),
        selected_entity_set_index_
    );
    appendLog(
        "ENTITYSET",
        "Added selection to " + entity_set.name +
            " (" + std::to_string(entity_set.members.totalCount()) + " entities total)."
    );
}

void EditorApplication::selectEntitySet(std::size_t index) {
    if (!active_document_.has_value() || index >= active_document_->entity_sets.size()) {
        return;
    }

    viewport_renderer_.setSelection(active_document_->entity_sets[index].members);
    selected_entity_set_index_ = index;
    appendLog(
        "ENTITYSET",
        "Selected " + active_document_->entity_sets[index].name +
            " (" + std::to_string(active_document_->entity_sets[index].members.totalCount()) + " entities)."
    );
}

void EditorApplication::bumpMeshRevision() {
    if (!active_document_.has_value()) {
        return;
    }

    active_document_->mesh_revision = next_mesh_revision_id_++;
    std::cout
        << "[HISTORYDBG] assigned mesh revision=" << active_document_->mesh_revision
        << " triangles=" << active_document_->triangles.size()
        << " vertices=" << active_document_->positions.size()
        << std::endl;
}

void EditorApplication::resetDocumentHistory(std::string root_label) {
    history_controller_.reset(active_document_ ? &active_document_.value() : nullptr, std::move(root_label));
    if (active_document_.has_value()) {
        next_mesh_revision_id_ = std::max(next_mesh_revision_id_, active_document_->mesh_revision + 1U);
    }
}

void EditorApplication::commitDocumentHistory(
    std::string label,
    const mesh::EntitySelection& selection,
    std::optional<std::size_t> selected_entity_set_index
) {
    history_controller_.commit(
        active_document_ ? &active_document_.value() : nullptr,
        std::move(label),
        selection,
        selected_entity_set_index
    );
}

void EditorApplication::undoDocumentHistory() {
    const std::optional<DocumentHistoryRestoreTarget> target = history_controller_.undoTarget();
    if (!target.has_value()) {
        if (active_document_.has_value()) {
            appendLog("HISTORY", "Undo unavailable.");
        }
        return;
    }

    beginHistoryRestoreOperation(*target, "Undo");
}

void EditorApplication::redoDocumentHistory() {
    const std::optional<DocumentHistoryRestoreTarget> target = history_controller_.redoTarget();
    if (!target.has_value()) {
        if (active_document_.has_value()) {
            appendLog("HISTORY", "Redo unavailable.");
        }
        return;
    }

    beginHistoryRestoreOperation(*target, "Redo");
}

void EditorApplication::jumpToDocumentHistoryNode(std::size_t node_id) {
    const std::optional<DocumentHistoryRestoreTarget> target = history_controller_.targetByNodeId(node_id);
    if (!target.has_value()) {
        return;
    }

    beginHistoryRestoreOperation(*target, "Jumped to");
}

std::string EditorApplication::makeDefaultEntitySetName() const {
    std::size_t suffix = 1;
    while (active_document_.has_value()) {
        const std::string candidate = "Entity Set " + std::to_string(suffix);
        const auto match = std::find_if(
            active_document_->entity_sets.begin(),
            active_document_->entity_sets.end(),
            [&candidate](const mesh::EntitySet& entity_set) {
                return entity_set.name == candidate;
            }
        );
        if (match == active_document_->entity_sets.end()) {
            return candidate;
        }
        ++suffix;
    }

    return "Entity Set 1";
}

}  // namespace meshtools::app
