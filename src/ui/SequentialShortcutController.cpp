#include "meshtools/ui/SequentialShortcutController.h"

#include <algorithm>
#include <cctype>

namespace meshtools::ui {
namespace {

constexpr double kShortcutMenuOpenDelaySeconds = 0.05;
constexpr float kShortcutMenuRowHeight = 20.0F;
constexpr float kShortcutMenuMaxVisibleRows = 8.0F;
constexpr float kShortcutMenuWidth = 220.0F;
constexpr float kShortcutMouseDeadzone = 10.0F;
constexpr ImVec2 kShortcutMenuAnchorOffset = ImVec2(-50.0F, -20.0F);
constexpr const char* kShortcutMenuWindowName = "SequentialShortcutMenu";

bool isCmdOrCtrlHeld(const ImGuiIO& io) {
#if defined(__APPLE__)
    return io.KeySuper;
#else
    return io.KeyCtrl;
#endif
}

}  // namespace

void SequentialShortcutController::registerShortcut(
    ImGuiKey first_key,
    ImGuiKey second_key,
    const char* label,
    ShortcutCommand command
) {
    bindings_.push_back(Binding{
        .first_key = first_key,
        .second_key = second_key,
        .label = label,
        .command = command,
    });
}

void SequentialShortcutController::handleInput(const std::function<void(ShortcutCommand)>& on_trigger) {
    ImGuiIO& io = ImGui::GetIO();
    if (isCmdOrCtrlHeld(io)) {
        reset();
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        reset();
        return;
    }

    std::vector<ImGuiKey> pressed_first_keys;
    std::vector<ImGuiKey> pressed_second_keys;
    pressed_first_keys.reserve(bindings_.size());
    pressed_second_keys.reserve(bindings_.size());

    for (const Binding& binding : bindings_) {
        if (ImGui::IsKeyPressed(binding.first_key, false) &&
            std::find(pressed_first_keys.begin(), pressed_first_keys.end(), binding.first_key) == pressed_first_keys.end()) {
            pressed_first_keys.push_back(binding.first_key);
        }

        if (ImGui::IsKeyPressed(binding.second_key, false) &&
            std::find(pressed_second_keys.begin(), pressed_second_keys.end(), binding.second_key) == pressed_second_keys.end()) {
            pressed_second_keys.push_back(binding.second_key);
        }
    }

    const auto try_trigger_second_key = [&](ImGuiKey first_key) -> bool {
        for (ImGuiKey second_key : pressed_second_keys) {
            const auto match = std::find_if(
                bindings_.begin(),
                bindings_.end(),
                [first_key, second_key](const Binding& binding) {
                    return binding.first_key == first_key && binding.second_key == second_key;
                }
            );
            if (match != bindings_.end()) {
                on_trigger(match->command);
                reset();
                return true;
            }
        }

        return false;
    };

    if (pending_.first_key != ImGuiKey_None) {
        if (try_trigger_second_key(pending_.first_key)) {
            return;
        }

        for (ImGuiKey first_key : pressed_first_keys) {
            if (first_key != pending_.first_key) {
                beginSequence(first_key, io.MousePos);
                return;
            }
        }

        return;
    }

    for (ImGuiKey first_key : pressed_first_keys) {
        beginSequence(first_key, io.MousePos);
        if (try_trigger_second_key(first_key)) {
            return;
        }
        return;
    }
}

void SequentialShortcutController::drawMenu(const std::function<void(ShortcutCommand)>& on_trigger) {
    if (pending_.first_key == ImGuiKey_None) {
        return;
    }

    if ((ImGui::GetTime() - pending_.started_at_seconds) < kShortcutMenuOpenDelaySeconds) {
        return;
    }

    pending_.menu_visible = true;

    std::vector<const Binding*> matching_bindings;
    matching_bindings.reserve(bindings_.size());
    for (const Binding& binding : bindings_) {
        if (binding.first_key == pending_.first_key) {
            matching_bindings.push_back(&binding);
        }
    }

    if (matching_bindings.empty()) {
        reset();
        return;
    }

    ImGui::SetNextWindowPos(pending_.menu_anchor, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(kShortcutMenuWidth, 0.0F), ImVec2(kShortcutMenuWidth, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(0.96F);

    constexpr ImGuiWindowFlags menu_flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoNav;

    bool should_close = false;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0F, 4.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    if (ImGui::Begin(kShortcutMenuWindowName, nullptr, menu_flags)) {
        const float child_height = std::min(
            static_cast<float>(matching_bindings.size()) * kShortcutMenuRowHeight,
            kShortcutMenuMaxVisibleRows * kShortcutMenuRowHeight
        );

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0F, 2.0F));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 0.0F));
        ImGui::BeginChild("ShortcutOptions", ImVec2(0.0F, child_height), ImGuiChildFlags_None);
        for (std::size_t index = 0; index < matching_bindings.size(); ++index) {
            const Binding& binding = *matching_bindings[index];
            const char* shortcut_label = ImGui::GetKeyName(binding.second_key);

            ImGui::PushID(static_cast<int>(index));
            if (ImGui::MenuItem(binding.label, shortcut_label)) {
                on_trigger(binding.command);
                should_close = true;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        const bool menu_hovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows
        );
        const ImVec2 menu_position = ImGui::GetWindowPos();
        const ImVec2 menu_size = ImGui::GetWindowSize();
        if (menu_hovered) {
            pending_.menu_hovered_once = true;
        }
        ImGui::End();
        ImGui::PopStyleVar(2);

        if (should_close) {
            reset();
            return;
        }

        const ImVec2 mouse_position = ImGui::GetIO().MousePos;
        const bool mouse_outside_deadzone =
            mouse_position.x < (menu_position.x - kShortcutMouseDeadzone) ||
            mouse_position.x > (menu_position.x + menu_size.x + kShortcutMouseDeadzone) ||
            mouse_position.y < (menu_position.y - kShortcutMouseDeadzone) ||
            mouse_position.y > (menu_position.y + menu_size.y + kShortcutMouseDeadzone);
        const bool mouse_clicked_outside =
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (!menu_hovered && pending_.menu_visible && (mouse_outside_deadzone || mouse_clicked_outside)) {
            reset();
        }

        return;
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}

std::string SequentialShortcutController::shortcutLabel(ShortcutCommand command) const {
    const auto match = std::find_if(
        bindings_.begin(),
        bindings_.end(),
        [command](const Binding& binding) {
            return binding.command == command;
        }
    );
    if (match == bindings_.end()) {
        return {};
    }

    const char* first_key_name = ImGui::GetKeyName(match->first_key);
    const char* second_key_name = ImGui::GetKeyName(match->second_key);
    if (first_key_name == nullptr || second_key_name == nullptr) {
        return {};
    }

    std::string label = first_key_name;
    if (!label.empty()) {
        label += "";
    }
    label += second_key_name;
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return label;
}

void SequentialShortcutController::beginSequence(ImGuiKey first_key, const ImVec2& menu_anchor) {
    pending_ = PendingState{
        .first_key = first_key,
        .menu_anchor = ImVec2(
            menu_anchor.x + kShortcutMenuAnchorOffset.x,
            menu_anchor.y + kShortcutMenuAnchorOffset.y
        ),
        .started_at_seconds = ImGui::GetTime(),
        .menu_visible = false,
        .menu_hovered_once = false,
    };
}

void SequentialShortcutController::reset() {
    pending_ = PendingState{};
}

}  // namespace meshtools::ui
