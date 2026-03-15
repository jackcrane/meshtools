#include "meshtools/ui/UIOperations/SelectSimilar.h"

#include <algorithm>

namespace meshtools::ui {
namespace {

constexpr const char* kSelectSimilarDialogName = "Select Similar";

}  // namespace

void SelectSimilarOperation::openDialog() {
    dialog_pending_open_ = true;
}

void SelectSimilarOperation::draw(const EditorUiState& state, EditorUiActions* actions) {
    if (dialog_pending_open_) {
        dialog_pending_open_ = false;
        dialog_open_ = true;
        ImGui::OpenPopup(kSelectSimilarDialogName);
    }

    if (!dialog_open_ && !ImGui::IsPopupOpen(kSelectSimilarDialogName)) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(420.0F, 0.0F), ImGuiCond_Appearing);
    bool keep_open = dialog_open_;
    if (!ImGui::BeginPopupModal(kSelectSimilarDialogName, &keep_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        dialog_open_ = keep_open && ImGui::IsPopupOpen(kSelectSimilarDialogName);
        return;
    }

    if (actions != nullptr) {
        actions->select_similar_dialog_open = true;
        actions->request_select_similar = EditorUiActions::SelectSimilarRequest{
            .config = config_,
            .intent = EditorUiActions::SelectSimilarRequest::Intent::Preview,
        };
    }

    ImGui::Checkbox("Allow rotation in X", &config_.allow_rotation_x);
    ImGui::Checkbox("Allow rotation in Y", &config_.allow_rotation_y);
    ImGui::Checkbox("Allow rotation in Z", &config_.allow_rotation_z);
    ImGui::Checkbox("Allow scaling", &config_.allow_scaling);
    ImGui::BeginDisabled(!config_.allow_scaling);
    ImGui::Checkbox("Require uniform scaling", &config_.require_uniform_scaling);
    ImGui::EndDisabled();
    ImGui::InputFloat("Tolerance (%)", &config_.tolerance, 0.1F, 1.0F, "%.3f");
    config_.tolerance = std::max(config_.tolerance, 0.0F);

    ImGui::Spacing();
    if (state.select_similar_feedback.available) {
        ImGui::Text(
            "Preview: %zu groups, +%zu edges",
            state.select_similar_feedback.match_count,
            state.select_similar_feedback.preview_edge_count
        );
    } else if (!state.select_similar_feedback.unavailable_reasons.empty()) {
        ImGui::SeparatorText("Unavailable");
        for (const std::string& reason : state.select_similar_feedback.unavailable_reasons) {
            ImGui::TextWrapped("- %s", reason.c_str());
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::BeginDisabled(!state.select_similar_feedback.available);
    if (ImGui::Button("Add to Selection", ImVec2(160.0F, 0.0F)) && actions != nullptr) {
        actions->request_select_similar = EditorUiActions::SelectSimilarRequest{
            .config = config_,
            .intent = EditorUiActions::SelectSimilarRequest::Intent::AddToSelection,
        };
        keep_open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0F, 0.0F))) {
        keep_open = false;
        ImGui::CloseCurrentPopup();
    }

    dialog_open_ = keep_open;
    ImGui::EndPopup();
}

}  // namespace meshtools::ui
