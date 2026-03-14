#include "meshtools/app/EditorApplication.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
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
        if (pending_renderer_selection_.has_value()) {
            viewport_renderer_.setSelection(*pending_renderer_selection_);
            pending_renderer_selection_.reset();
        }
        editor_ui_.setViewportTexture(viewport_renderer_.textureId());

        const ImVec4& clear_color = editor_ui_.clearColor();
        window_.beginFrame(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        editor_ui_.beginFrame();

        const ui::EditorUiState ui_state{
            .modify_delete_availability =
                active_document_
                    ? mesh::computeModifyDeleteAvailability(active_document_.value(), viewport_renderer_.currentSelection())
                    : mesh::ModifyDeleteAvailability{},
            .modify_create_face_availability =
                active_document_
                    ? mesh::computeModifyCreateFaceAvailability(active_document_.value(), viewport_renderer_.currentSelection())
                    : mesh::ModifyCreateFaceAvailability{},
            .modify_project_availability =
                active_document_
                    ? mesh::computeModifyProjectAvailability(active_document_.value(), viewport_renderer_.currentSelection())
                    : mesh::ModifyProjectAvailability{},
            .current_selection = viewport_renderer_.currentSelection(),
            .active_document = active_document_ ? &active_document_.value() : nullptr,
            .entity_sets = active_document_ ? std::span<const mesh::EntitySet>(active_document_->entity_sets) : std::span<const mesh::EntitySet>{},
            .selected_entity_set_index = selected_entity_set_index_,
            .log_messages = log_messages_,
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
        };
        const ui::EditorUiActions actions = editor_ui_.draw(ui_state);
        editor_ui_.endFrame(window_.nativeHandle());
        window_.swapBuffers();

        pending_viewport_camera_input_ = actions.viewport_camera;
        for (const ui::EditorUiLogEvent& event_log : actions.event_logs) {
            appendLog(event_log.origin, event_log.message);
        }
        handleViewportSelectionRequest(actions.viewport_selection);
        if (actions.request_select_edge_loop) {
            handleSelectEdgeLoopRequest();
        }
        if (actions.request_invert_selection) {
            handleInvertSelectionRequest();
        }
        handleExpandSelectionActions(actions);
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
    std::string log_line = '[' + makeTimestamp() + "][" + std::move(origin) + "] " + std::move(message);
    std::cout << log_line << std::endl;
    log_messages_.push_back(std::move(log_line));
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
    mesh::ensureRenderableNormals(&active_document_.value());
    viewport_renderer_.clearSelection();
    viewport_renderer_.clearExpandSelectionPreview();
    active_document_->display_name_override.clear();
    active_document_->up_axis = editor_ui_.fileImportSettings().up_axis;
    active_project_path_.clear();
    selected_entity_set_index_.reset();
    pending_renderer_selection_.reset();
    expand_selection_feedback_reasons_.clear();
    expand_selection_feedback_ = {};
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
    mesh::ensureRenderableNormals(&active_document_.value());
    viewport_renderer_.clearSelection();
    viewport_renderer_.clearExpandSelectionPreview();
    active_project_path_ = path;
    selected_entity_set_index_.reset();
    pending_renderer_selection_.reset();
    log_messages_ = std::move(result.log_messages);
    expand_selection_feedback_reasons_.clear();
    expand_selection_feedback_ = {};
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

    const mesh::ModifyCreateFaceResult result =
        mesh::applyModifyCreateFace(&active_document_.value(), viewport_renderer_.currentSelection());
    if (!result.changed) {
        appendLog("MODIFY", "Create face skipped because the selection could not form a face.");
        return;
    }

    viewport_renderer_.clearSelection();
    viewport_renderer_.clearExpandSelectionPreview();
    expand_selection_feedback_reasons_.clear();
    expand_selection_feedback_ = {};
    selected_entity_set_index_.reset();
    pending_renderer_selection_ = mesh::EntitySelection{
        .edge_indices = {},
        .face_indices = result.created_face_indices,
        .point_indices = {},
    };
    appendLog("MODIFY", "Created " + std::to_string(result.created_face_count) + " faces.");
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
    const mesh::ModifyProjectResult result =
        mesh::applyModifyProject(&active_document_.value(), options);
    if (!result.changed) {
        appendLog("MODIFY", "Project skipped because the requested line could not be created.");
        return;
    }

    viewport_renderer_.clearSelection();
    viewport_renderer_.clearExpandSelectionPreview();
    expand_selection_feedback_reasons_.clear();
    expand_selection_feedback_ = {};
    selected_entity_set_index_.reset();
    pending_renderer_selection_ = mesh::EntitySelection{
        .edge_indices = result.created_edge_index.has_value()
            ? std::vector<std::uint32_t>{*result.created_edge_index}
            : std::vector<std::uint32_t>{},
        .face_indices = {},
        .point_indices = {},
    };
    appendLog(
        "MODIFY",
        "Projected 2 faces and created 1 edge."
    );
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
    const mesh::ModifyDeleteResult result =
        mesh::applyModifyDelete(&active_document_.value(), viewport_renderer_.currentSelection(), options);
    if (!result.changed) {
        appendLog("MODIFY", "Delete skipped because nothing applicable was selected.");
        return;
    }

    viewport_renderer_.clearSelection();
    viewport_renderer_.clearExpandSelectionPreview();
    expand_selection_feedback_reasons_.clear();
    expand_selection_feedback_ = {};
    selected_entity_set_index_.reset();
    pending_renderer_selection_.reset();
    appendLog(
        "MODIFY",
        "Deleted " + std::to_string(result.deleted_face_count) + " faces and " +
            std::to_string(result.deleted_point_count) + " points."
    );
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

            active_document_->entity_sets[index].name = trimmed_name;
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
    appendUniqueIndices(entity_set.members.edge_indices, selection.edge_indices);
    appendUniqueIndices(entity_set.members.face_indices, selection.face_indices);
    appendUniqueIndices(entity_set.members.point_indices, selection.point_indices);
    selected_entity_set_index_ = index;
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
