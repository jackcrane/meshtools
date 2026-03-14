#include "meshtools/ui/UIOperations/ModifyProject.h"

#include <vector>

namespace meshtools::ui {
namespace {

constexpr const char* kModifyProjectDialogName = "Modify Project";

bool containsFace(const std::array<std::uint32_t, 2>& source_faces, std::uint32_t face_index) {
    return face_index == source_faces[0] || face_index == source_faces[1];
}

std::string modifyProjectTargetLabel(const mesh::ModifyProjectTarget& target) {
    switch (target.type) {
        case mesh::ModifyProjectTargetType::Face:
            return "Face " + std::to_string(target.index);
        case mesh::ModifyProjectTargetType::Edge:
            return "Edge " + std::to_string(target.index);
        case mesh::ModifyProjectTargetType::Point:
            return "Point " + std::to_string(target.index);
    }

    return "Unknown";
}

std::optional<mesh::ModifyProjectTarget> captureModifyProjectTarget(
    const mesh::EntitySelection& selection,
    const std::array<std::uint32_t, 2>& source_faces,
    std::string* feedback
) {
    std::vector<std::uint32_t> faces;
    faces.reserve(selection.face_indices.size());
    for (const std::uint32_t face_index : selection.face_indices) {
        if (!containsFace(source_faces, face_index)) {
            faces.push_back(face_index);
        }
    }

    const std::size_t target_count = faces.size() + selection.edge_indices.size() + selection.point_indices.size();
    if (target_count != 1U) {
        if (feedback != nullptr) {
            *feedback = "Select exactly one terminator entity besides the 2 source faces.";
        }
        return std::nullopt;
    }

    if (!faces.empty()) {
        return mesh::ModifyProjectTarget{
            .type = mesh::ModifyProjectTargetType::Face,
            .index = faces.front(),
        };
    }
    if (!selection.edge_indices.empty()) {
        return mesh::ModifyProjectTarget{
            .type = mesh::ModifyProjectTargetType::Edge,
            .index = selection.edge_indices.front(),
        };
    }
    if (!selection.point_indices.empty()) {
        return mesh::ModifyProjectTarget{
            .type = mesh::ModifyProjectTargetType::Point,
            .index = selection.point_indices.front(),
        };
    }

    if (feedback != nullptr) {
        *feedback = "Select exactly one terminator entity.";
    }
    return std::nullopt;
}

}  // namespace

void ModifyProjectOperation::openDialog() {
    dialog_pending_open_ = true;
}

void ModifyProjectOperation::draw(const EditorUiState& state, EditorUiActions* actions) {
    if (dialog_pending_open_) {
        dialog_pending_open_ = false;
        if (state.modify_project_availability.any() && state.current_selection.face_indices.size() == 2U) {
            source_faces_[0] = state.current_selection.face_indices[0];
            source_faces_[1] = state.current_selection.face_indices[1];
            infinite_length_ = true;
            start_ = {};
            end_ = {};
            capture_feedback_.clear();
            dialog_open_ = true;
        }
    }

    if (!dialog_open_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0F, 0.0F), ImGuiCond_Appearing);
    bool keep_open = dialog_open_;
    if (!ImGui::Begin(kModifyProjectDialogName, &keep_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        dialog_open_ = keep_open;
        return;
    }

    ImGui::Text("Source Faces: %u and %u", source_faces_[0], source_faces_[1]);
    ImGui::Checkbox("Infinite Length", &infinite_length_);

    if (!infinite_length_) {
        const std::string start_label =
            start_.target.has_value() ? modifyProjectTargetLabel(*start_.target) : std::string("Unset");
        const std::string end_label =
            end_.target.has_value() ? modifyProjectTargetLabel(*end_.target) : std::string("Unset");

        ImGui::SeparatorText("Terminator Start");
        ImGui::Text("Current: %s", start_label.c_str());
        if (ImGui::Button("Use Current Selection for Start")) {
            if (const auto target = captureModifyProjectTarget(
                    state.current_selection,
                    source_faces_,
                    &capture_feedback_
                );
                target.has_value()) {
                start_.target = *target;
                capture_feedback_ = "Captured " + modifyProjectTargetLabel(*target) + " as Start.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Start")) {
            start_.target.reset();
        }

        ImGui::SeparatorText("Terminator End");
        ImGui::Text("Current: %s", end_label.c_str());
        if (ImGui::Button("Use Current Selection for End")) {
            if (const auto target = captureModifyProjectTarget(
                    state.current_selection,
                    source_faces_,
                    &capture_feedback_
                );
                target.has_value()) {
                end_.target = *target;
                capture_feedback_ = "Captured " + modifyProjectTargetLabel(*target) + " as End.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear End")) {
            end_.target.reset();
        }

        ImGui::TextWrapped(
            "Select one face, edge, or point in the viewport, then capture it for Start and End. "
            "Source faces are ignored during capture."
        );
    }

    if (!capture_feedback_.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", capture_feedback_.c_str());
    }

    const bool can_apply = infinite_length_ || (start_.target.has_value() && end_.target.has_value());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::BeginDisabled(!can_apply);
    if (ImGui::Button("Apply", ImVec2(120.0F, 0.0F)) && actions != nullptr) {
        actions->request_modify_project = EditorUiActions::ModifyProjectRequest{
            .source_face_indices = source_faces_,
            .infinite_length = infinite_length_,
            .start_target = start_.target,
            .end_target = end_.target,
        };
        keep_open = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0F, 0.0F))) {
        keep_open = false;
    }

    ImGui::End();
    dialog_open_ = keep_open;
}

}  // namespace meshtools::ui
