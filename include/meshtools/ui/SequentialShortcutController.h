#pragma once

#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

namespace meshtools::ui {

enum class ShortcutCommand {
    ToggleWireframe,
    ToggleShadeTriangles,
    ToggleShowPoints,
    ToggleEdgeSelection,
    ToggleFaceSelection,
    TogglePointSelection,
    ResetViewport,
};

class SequentialShortcutController {
  public:
    void registerShortcut(
        ImGuiKey first_key,
        ImGuiKey second_key,
        const char* label,
        ShortcutCommand command
    );

    void handleInput(const std::function<void(ShortcutCommand)>& on_trigger);
    void drawMenu(const std::function<void(ShortcutCommand)>& on_trigger);
    [[nodiscard]] std::string shortcutLabel(ShortcutCommand command) const;

  private:
    struct Binding {
        ImGuiKey first_key = ImGuiKey_None;
        ImGuiKey second_key = ImGuiKey_None;
        const char* label = "";
        ShortcutCommand command = ShortcutCommand::ToggleWireframe;
    };

    struct PendingState {
        ImGuiKey first_key = ImGuiKey_None;
        ImVec2 menu_anchor = ImVec2(0.0F, 0.0F);
        double started_at_seconds = 0.0;
        bool first_key_released = false;
        bool menu_visible = false;
        bool menu_hovered_once = false;
    };

    void beginSequence(ImGuiKey first_key, const ImVec2& menu_anchor);
    void reset();

    std::vector<Binding> bindings_;
    PendingState pending_{};
};

}  // namespace meshtools::ui
