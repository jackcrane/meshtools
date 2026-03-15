#include "meshtools/ui/BottomPane.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "meshtools/ui/EditorDockLayout.h"

namespace meshtools::ui {

namespace {

[[nodiscard]] std::string formatEntityIds(const std::vector<std::uint32_t>& indices) {
    if (indices.empty()) {
        return "None";
    }

    constexpr std::size_t kMaxDisplayedIds = 24;

    std::string result;
    for (std::size_t index = 0; index < indices.size() && index < kMaxDisplayedIds; ++index) {
        if (!result.empty()) {
            result += ", ";
        }

        result += std::to_string(indices[index]);
    }

    if (indices.size() > kMaxDisplayedIds) {
        result += ", ... (+";
        result += std::to_string(indices.size() - kMaxDisplayedIds);
        result += " more)";
    }

    return result;
}

}  // namespace

void BottomPane::draw(const EditorUiState& state) {
    constexpr ImGuiWindowFlags pane_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove;

    ImGui::Begin(kEditorBottomPaneWindowName, nullptr, pane_flags);
    if (ImGui::BeginTabBar("BottomTabs")) {
        if (ImGui::BeginTabItem("Console")) {
            if (state.log_messages.empty()) {
                ImGui::TextDisabled("No log messages.");
            } else {
                const bool appended_log_messages = state.log_messages.size() != last_console_log_count_;
                const ImVec4 console_background = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];

                ImGui::PushStyleColor(ImGuiCol_FrameBg, console_background);
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, console_background);
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, console_background);
                if (ImGui::BeginChild("ConsoleOutput", ImVec2(0.0F, 0.0F), ImGuiChildFlags_FrameStyle)) {
                    for (std::size_t index = 0; index < state.log_messages.size(); ++index) {
                        ImGui::TextUnformatted(state.log_messages[index].c_str());
                    }

                    if (appended_log_messages) {
                        ImGui::SetScrollHereY(1.0F);
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor(3);
            }
            last_console_log_count_ = state.log_messages.size();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stats")) {
            ImGuiIO& io = ImGui::GetIO();
            const float frame_time_ms = io.Framerate > 0.0F ? (1000.0F / io.Framerate) : 0.0F;
            ImGui::Text("Frame time: %.3f ms", frame_time_ms);
            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::Text("Display size: %.0f x %.0f", io.DisplaySize.x, io.DisplaySize.y);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Selection")) {
            if (state.active_document != nullptr) {
                ImGui::Text("Active mesh: %s", state.active_document->displayName().c_str());
                ImGui::Text("Selected entities: %zu", state.selection_summary.totalCount());
                ImGui::Text("Faces: %zu", state.selection_summary.face_count);
                ImGui::TextWrapped("Face IDs: %s", formatEntityIds(state.current_selection.face_indices).c_str());
                ImGui::Text("Edges: %zu", state.selection_summary.edge_count);
                ImGui::TextWrapped("Edge IDs: %s", formatEntityIds(state.current_selection.edge_indices).c_str());
                ImGui::Text("Points: %zu", state.selection_summary.point_count);
                ImGui::TextWrapped("Point IDs: %s", formatEntityIds(state.current_selection.point_indices).c_str());
            } else {
                ImGui::TextUnformatted("No active mesh.");
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::End();
}

}  // namespace meshtools::ui
