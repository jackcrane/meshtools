#include "meshtools/ui/LeftPane.h"

#include <algorithm>
#include <cstring>

#include "meshtools/ui/EditorDockLayout.h"

namespace meshtools::ui {

void LeftPane::draw(const EditorUiState& state, EditorUiActions* actions) {
    constexpr ImGuiWindowFlags pane_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove;

    if (!state.active_document || (renaming_entity_set_index_.has_value() && *renaming_entity_set_index_ >= state.entity_sets.size())) {
        renaming_entity_set_index_.reset();
        focus_entity_set_rename_ = false;
    }

    ImGui::Begin(kEditorLeftPaneWindowName, nullptr, pane_flags);
    ImGui::TextUnformatted("Scene");
    ImGui::Separator();
    if (state.active_document != nullptr) {
        const bool document_selected = !state.selected_entity_set_index.has_value();
        if (ImGui::Selectable(state.active_document->displayName().c_str(), document_selected) && actions != nullptr) {
            actions->request_select_document_scene_item = true;
        }

        ImGui::Indent();
        for (std::size_t index = 0; index < state.entity_sets.size(); ++index) {
            const mesh::EntitySet& entity_set = state.entity_sets[index];
            const bool is_selected =
                state.selected_entity_set_index.has_value() && *state.selected_entity_set_index == index;
            ImGui::PushID(static_cast<int>(index));

            if (renaming_entity_set_index_.has_value() && *renaming_entity_set_index_ == index) {
                if (focus_entity_set_rename_) {
                    ImGui::SetKeyboardFocusHere();
                    focus_entity_set_rename_ = false;
                }

                const bool submitted = ImGui::InputText(
                    "##EntitySetRename",
                    rename_buffer_.data(),
                    rename_buffer_.size(),
                    ImGuiInputTextFlags_EnterReturnsTrue |
                        ImGuiInputTextFlags_AutoSelectAll
                );
                const bool item_deactivated = ImGui::IsItemDeactivatedAfterEdit();
                const bool cancel_requested = ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
                if ((submitted || item_deactivated) && actions != nullptr) {
                    actions->request_rename_entity_set = EditorUiActions::EntitySetRenameRequest{
                        .index = index,
                        .name = rename_buffer_.data(),
                    };
                    renaming_entity_set_index_.reset();
                } else if (cancel_requested) {
                    renaming_entity_set_index_.reset();
                }
            } else {
                if (ImGui::Selectable(entity_set.name.c_str(), is_selected) && actions != nullptr) {
                    actions->request_select_entity_set_index = index;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    beginRenamingEntitySet(index, entity_set.name);
                }
            }
            ImGui::PopID();
        }
        if (state.entity_sets.empty()) {
            ImGui::TextDisabled("No entity sets");
        }
        ImGui::Unindent();
    } else {
        ImGui::TextDisabled("No project loaded");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Inspector");
    if (state.active_document != nullptr) {
        mesh::MeshDocument& document = *state.active_document;
        ImGui::Text("Path: %s", document.source_path.string().c_str());
        ImGui::Text("Format: %s", document.formatLabel().c_str());
        ImGui::Text("Vertices: %zu", document.positions.size());
        ImGui::Text("Triangles: %zu", document.triangles.size());
        ImGui::Text("Normals: %zu", document.normals.size());

        int selected_up_axis = document.up_axis == UpAxis::Y ? 0 : 1;
        constexpr const char* up_axis_options[] = {"Y", "Z"};
        ImGui::SetNextItemWidth(88.0F);
        if (ImGui::Combo("Project up axis", &selected_up_axis, up_axis_options, IM_ARRAYSIZE(up_axis_options))) {
            document.up_axis = selected_up_axis == 0 ? UpAxis::Y : UpAxis::Z;
            if (actions != nullptr) {
                actions->event_logs.push_back(EditorUiLogEvent{
                    .origin = "PROJECT",
                    .message = std::string("Up axis set to ") + mesh::upAxisName(document.up_axis) + ".",
                });
            }
        }

        if (document.bounds.valid) {
            ImGui::SeparatorText("Bounds");
            ImGui::Text("Min: %.3f %.3f %.3f", document.bounds.minimum.x, document.bounds.minimum.y, document.bounds.minimum.z);
            ImGui::Text("Max: %.3f %.3f %.3f", document.bounds.maximum.x, document.bounds.maximum.y, document.bounds.maximum.z);
        }
    } else {
        ImGui::TextWrapped("Use File > Open to load a .mt project or import an OBJ/STL mesh.");
    }

    ImGui::End();
}

void LeftPane::beginRenamingEntitySet(std::size_t index, std::string_view current_name) {
    renaming_entity_set_index_ = index;
    focus_entity_set_rename_ = true;
    std::fill(rename_buffer_.begin(), rename_buffer_.end(), '\0');

    const std::size_t copy_length = std::min(current_name.size(), rename_buffer_.size() - 1U);
    std::memcpy(rename_buffer_.data(), current_name.data(), copy_length);
    rename_buffer_[copy_length] = '\0';
}

}  // namespace meshtools::ui
