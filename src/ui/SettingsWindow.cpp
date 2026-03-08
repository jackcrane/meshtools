#include "meshtools/ui/SettingsWindow.h"

namespace meshtools::ui {

void SettingsWindow::open() {
    open_ = true;
}

void SettingsWindow::draw(
    ViewportControlSettings& viewport_control_settings,
    GraphicsQualitySettings& graphics_quality_settings,
    FileImportSettings& file_import_settings,
    EditorUiActions* actions
) {
    if (!open_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(720.0F, 420.0F), ImGuiCond_FirstUseEver);

    constexpr ImGuiWindowFlags settings_window_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking;

    if (!ImGui::Begin("Settings", &open_, settings_window_flags)) {
        ImGui::End();
        return;
    }

    ImGui::BeginChild("SettingsSections", ImVec2(180.0F, 0.0F), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    if (ImGui::Selectable("Viewport controls", selected_section_ == 0)) {
        selected_section_ = 0;
    }
    if (ImGui::Selectable("Graphics quality", selected_section_ == 1)) {
        selected_section_ = 1;
    }
    if (ImGui::Selectable("File import", selected_section_ == 2)) {
        selected_section_ = 2;
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("SettingsContent", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders);
    if (selected_section_ == 0) {
        ImGui::TextUnformatted("Viewport controls");
        ImGui::Separator();
        if (ImGui::Checkbox("Invert Y movement", &viewport_control_settings.invert_y_movement) && actions != nullptr) {
            actions->event_logs.push_back(EditorUiLogEvent{
                .origin = "SETTINGS",
                .message = std::string("Invert Y movement ") +
                    (viewport_control_settings.invert_y_movement ? "enabled." : "disabled."),
            });
        }
        if (ImGui::Checkbox("Invert zoom", &viewport_control_settings.invert_zoom) && actions != nullptr) {
            actions->event_logs.push_back(EditorUiLogEvent{
                .origin = "SETTINGS",
                .message = std::string("Invert zoom ") +
                    (viewport_control_settings.invert_zoom ? "enabled." : "disabled."),
            });
        }

        int selected_pan_modifier = static_cast<int>(viewport_control_settings.pan_modifier);
        constexpr const char* pan_modifier_options[] = {"Cmd/Ctrl", "Shift", "Right-click"};
        if (ImGui::Combo("Pan modifier", &selected_pan_modifier, pan_modifier_options, IM_ARRAYSIZE(pan_modifier_options))) {
            viewport_control_settings.pan_modifier = static_cast<PanModifier>(selected_pan_modifier);
            if (actions != nullptr) {
                actions->event_logs.push_back(EditorUiLogEvent{
                    .origin = "SETTINGS",
                    .message = std::string("Pan modifier set to ") + panModifierName(viewport_control_settings.pan_modifier) + ".",
                });
            }
        }
    } else if (selected_section_ == 1) {
        ImGui::TextUnformatted("Graphics quality");
        ImGui::Separator();
        if (ImGui::SliderInt(
            "Render resolution",
            &graphics_quality_settings.render_resolution_percent,
            25,
            100,
            "%d%%"
        ) && actions != nullptr) {
            actions->event_logs.push_back(EditorUiLogEvent{
                .origin = "SETTINGS",
                .message = "Render resolution set to " +
                    std::to_string(graphics_quality_settings.render_resolution_percent) + "%.",
            });
        }
    } else if (selected_section_ == 2) {
        ImGui::TextUnformatted("File import");
        ImGui::Separator();

        int selected_up_axis = file_import_settings.up_axis == UpAxis::Y ? 0 : 1;
        constexpr const char* up_axis_options[] = {"Y", "Z"};
        if (ImGui::Combo("Up axis", &selected_up_axis, up_axis_options, IM_ARRAYSIZE(up_axis_options))) {
            file_import_settings.up_axis = selected_up_axis == 0 ? UpAxis::Y : UpAxis::Z;
            if (actions != nullptr) {
                actions->event_logs.push_back(EditorUiLogEvent{
                    .origin = "SETTINGS",
                    .message = std::string("Up axis set to ") + upAxisName(file_import_settings.up_axis) + ".",
                });
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Common conventions");
        ImGui::TextWrapped("Different formats and tools use different axis conventions. This setting will be used to interpret imported meshes.");
        ImGui::Spacing();
        ImGui::BulletText("OBJ: commonly Y-up");
        ImGui::BulletText("STL: no standard up axis, often treated as Z-up for CAD/manufacturing workflows");
        ImGui::BulletText("glTF: Y-up");
        ImGui::BulletText("FBX: often Y-up, but many DCC pipelines also use Z-up");
        ImGui::BulletText("Blender scenes: typically Z-up");
    }
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace meshtools::ui
