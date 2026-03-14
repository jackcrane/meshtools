#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "imgui.h"
#include "meshtools/mesh/MeshDocument.h"

namespace meshtools::ui {

struct EditorUiState {
    struct ExpandSelectionFeedback {
        bool available = false;
        bool linear_intersection_enabled = false;
        std::size_t preview_total_count = 0;
        std::size_t preview_face_count = 0;
        std::size_t preview_added_face_count = 0;
        std::span<const std::string> unavailable_reasons;
    };

    mesh::ModifyDeleteAvailability modify_delete_availability;
    mesh::ModifyCreateFaceAvailability modify_create_face_availability;

    mesh::MeshDocument* active_document = nullptr;
    std::span<const mesh::EntitySet> entity_sets;
    std::optional<std::size_t> selected_entity_set_index;
    std::span<const std::string> log_messages;
    float camera_yaw = 0.0F;
    float camera_pitch = 0.0F;
    struct SelectionSummary {
        std::size_t edge_count = 0;
        std::size_t face_count = 0;
        std::size_t point_count = 0;

        [[nodiscard]] std::size_t totalCount() const {
            return edge_count + face_count + point_count;
        }
    } selection_summary;
    ExpandSelectionFeedback expand_selection_feedback;
};

enum class PanModifier {
    CmdOrCtrl,
    Shift,
    RightClick,
};

inline const char* panModifierName(PanModifier modifier) {
    switch (modifier) {
        case PanModifier::CmdOrCtrl:
            return "Cmd/Ctrl";
        case PanModifier::Shift:
            return "Shift";
        case PanModifier::RightClick:
            return "Right-click";
    }

    return "Unknown";
}

struct SelectionFilters {
    bool edges = false;
    bool faces = true;
    bool points = false;
};

using UpAxis = mesh::UpAxis;

enum class ExpandSelectionMethod {
    Coplanar,
    Adjacent,
    IntersectingNormals,
};

struct ViewportControlSettings {
    bool invert_y_movement = true;
    bool invert_zoom = false;
    PanModifier pan_modifier = PanModifier::Shift;
};

struct GraphicsQualitySettings {
    int render_resolution_percent = 100;
};

struct FileImportSettings {
    UpAxis up_axis = UpAxis::Y;
};

struct ViewportDisplaySettings {
    bool show_wireframe = true;
    bool shade_triangles = true;
    bool show_points = false;
};

struct ViewportCameraInput {
    ImVec2 orbit_delta = ImVec2(0.0F, 0.0F);
    ImVec2 pan_delta = ImVec2(0.0F, 0.0F);
    float zoom_delta = 0.0F;
    bool reset = false;
};

struct ViewportSelectionRequest {
    enum class Type {
        None,
        Click,
        Box,
    };

    enum class Mode {
        Replace,
        Toggle,
        Path,
    };

    Type type = Type::None;
    Mode mode = Mode::Replace;
    float normalized_x = 0.0F;
    float normalized_y = 0.0F;
    float normalized_min_x = 0.0F;
    float normalized_min_y = 0.0F;
    float normalized_max_x = 0.0F;
    float normalized_max_y = 0.0F;
};

struct EditorUiLogEvent {
    std::string origin;
    std::string message;
};

struct EditorUiActions {
    struct ExpandSelectionConfig {
        ExpandSelectionMethod method = ExpandSelectionMethod::Coplanar;
        bool coplanar_include_parallel = false;
        bool coplanar_select_adjacent_only = true;
        float coplanar_tolerance_percent = 0.01F;
        float adjacent_max_angle_degrees = 10.0F;
        bool intersecting_include_inverse_normals = false;
        float intersecting_tolerance = 0.05F;
        bool intersecting_allow_linear_intersection = false;
    };

    struct ExpandSelectionRequest {
        enum class Intent {
            Preview,
            Select,
            CreateEntitySet,
            AddToExistingEntitySet,
        };

        ExpandSelectionConfig config;
        Intent intent = Intent::Preview;
        std::optional<std::size_t> entity_set_index;
    };

    struct EntitySetRenameRequest {
        std::size_t index = 0;
        std::string name;
    };

    struct ModifyDeleteRequest {
        bool faces = false;
        bool inside_edges = false;
        bool outside_edges = false;
        bool points = false;
    };

    bool request_exit = false;
    bool request_open_document = false;
    bool request_save_project = false;
    bool request_invert_selection = false;
    bool request_modify_create_face = false;
    bool request_add_selection_to_entity_set = false;
    bool request_create_entity_set_from_selection = false;
    bool request_select_document_scene_item = false;
    bool expand_selection_dialog_open = false;
    bool modify_delete_dialog_open = false;
    std::optional<std::size_t> request_add_selection_to_existing_entity_set_index;
    std::optional<std::size_t> request_select_entity_set_index;
    std::optional<EntitySetRenameRequest> request_rename_entity_set;
    std::optional<ExpandSelectionRequest> request_expand_selection;
    std::optional<ModifyDeleteRequest> request_modify_delete;
    ViewportCameraInput viewport_camera;
    ViewportSelectionRequest viewport_selection;
    std::vector<EditorUiLogEvent> event_logs;
};

}  // namespace meshtools::ui
