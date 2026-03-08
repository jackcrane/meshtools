#include "meshtools/ui/BottomPane.h"

#include "meshtools/ui/EditorDockLayout.h"
#include "imgui_internal.h"

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
                std::string console_text;
                for (std::size_t index = 0; index < state.log_messages.size(); ++index) {
                    console_text += state.log_messages[index];
                    if ((index + 1) < state.log_messages.size()) {
                        console_text.push_back('\n');
                    }
                }
                console_text.push_back('\0');

                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.02F, 0.02F, 0.02F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.02F, 0.02F, 0.02F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.02F, 0.02F, 0.02F, 1.0F));
                ImGui::InputTextMultiline(
                    "##ConsoleText",
                    console_text.data(),
                    console_text.size(),
                    ImVec2(-FLT_MIN, -FLT_MIN),
                    ImGuiInputTextFlags_ReadOnly
                );
                ImGui::PopStyleColor(3);

                if (state.log_messages.size() != last_console_log_count_) {
                    if (ImGuiWindow* console_text_window = ImGui::FindWindowByID(ImGui::GetItemID()); console_text_window != nullptr) {
                        console_text_window->Scroll.y = console_text_window->ScrollMax.y;
                    }
                }
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
            } else {
                ImGui::TextUnformatted("No active selection.");
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::End();
}

}  // namespace meshtools::ui
