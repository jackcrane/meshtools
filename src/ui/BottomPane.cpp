#include "meshtools/ui/BottomPane.h"

#include "meshtools/ui/EditorDockLayout.h"

namespace meshtools::ui {

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

                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.02F, 0.02F, 0.02F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.02F, 0.02F, 0.02F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.02F, 0.02F, 0.02F, 1.0F));
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
                ImGui::Text("Edges: %zu", state.selection_summary.edge_count);
                ImGui::Text("Points: %zu", state.selection_summary.point_count);
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
