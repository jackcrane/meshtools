#include "meshtools/ui/SettingsWindow.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <random>
#include <string>
#include <string_view>

namespace meshtools::ui {
namespace {

enum SettingsSection {
    kViewportConfigSection = 0,
    kGraphicsQualitySection = 1,
    kFileImportSection = 2,
    kAppearanceSection = 3,
};

constexpr std::array<int, 11> kThemePreviewColorIndices = {
    1, 5, 2, 8, 9, 10, 11, 12, 13, 14, 15,
};
constexpr float kThemeSwatchSize = 10.0F;
constexpr float kThemeSwatchGap = 2.0F;

std::string lowercase(std::string_view value) {
    std::string lowered(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return lowered;
}

bool matchesSearch(const ImGuiSystem::ThemeOption& theme, const char* query) {
    if (query == nullptr || query[0] == '\0') {
        return true;
    }

    const std::string lowered_query = lowercase(query);
    return lowercase(theme.scheme).find(lowered_query) != std::string::npos ||
           lowercase(theme.author).find(lowered_query) != std::string::npos ||
           lowercase(theme.id).find(lowered_query) != std::string::npos;
}

void appendSettingsLog(EditorUiActions* actions, const std::string& message) {
    if (actions == nullptr) {
        return;
    }

    actions->event_logs.push_back(EditorUiLogEvent{
        .origin = "SETTINGS",
        .message = message,
    });
}

void drawThemeRow(const ImGuiSystem::ThemeOption& theme, bool selected, float row_height) {
    const ImVec2 row_min = ImGui::GetItemRectMin();
    const ImVec2 row_max = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    const float text_y = row_min.y + std::max(0.0F, (row_height - ImGui::GetTextLineHeight()) * 0.5F);
    const float text_x = row_min.x + 8.0F;
    draw_list->AddText(ImVec2(text_x, text_y), ImGui::GetColorU32(ImGuiCol_Text), theme.scheme.c_str());

    const float swatch_strip_width =
        (static_cast<float>(kThemePreviewColorIndices.size()) * kThemeSwatchSize) +
        (static_cast<float>(kThemePreviewColorIndices.size() - 1) * kThemeSwatchGap);
    float swatch_x = row_max.x - 8.0F - swatch_strip_width;
    const float swatch_y = row_min.y + std::max(0.0F, (row_height - kThemeSwatchSize) * 0.5F);

    for (const int color_index : kThemePreviewColorIndices) {
        const ImVec2 min(swatch_x, swatch_y);
        const ImVec2 max(swatch_x + kThemeSwatchSize, swatch_y + kThemeSwatchSize);
        draw_list->AddRectFilled(
            min,
            max,
            ImGui::GetColorU32(theme.base_colors[static_cast<std::size_t>(color_index)]),
            2.0F
        );
        if (selected) {
            draw_list->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Text), 2.0F);
        }
        swatch_x += kThemeSwatchSize + kThemeSwatchGap;
    }
}

const char* themeSchemeById(const ImGuiSystem& imgui_system, const std::string& theme_id) {
    for (const ImGuiSystem::ThemeOption& theme : imgui_system.themes()) {
        if (theme.id == theme_id) {
            return theme.scheme.c_str();
        }
    }
    return "ImGUI";
}

bool previewRandomTheme(ImGuiSystem& imgui_system) {
    const std::vector<ImGuiSystem::ThemeOption>& themes = imgui_system.themes();
    if (themes.empty()) {
        return false;
    }

    static std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<std::size_t> distribution(0, themes.size() - 1);
    return imgui_system.previewThemeById(themes[distribution(generator)].id);
}

}  // namespace

void SettingsWindow::open() {
    open_ = true;
}

void SettingsWindow::draw(
    ViewportControlSettings& viewport_control_settings,
    GraphicsQualitySettings& graphics_quality_settings,
    FileImportSettings& default_file_import_settings,
    ImGuiSystem& imgui_system,
    EditorUiActions* actions
) {
    if (!open_) {
        return;
    }

    const bool was_open = open_;
    bool window_open = open_;

    ImGui::SetNextWindowSize(ImVec2(900.0F, 520.0F), ImGuiCond_FirstUseEver);

    constexpr ImGuiWindowFlags settings_window_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking;

    if (!ImGui::Begin("Settings", &window_open, settings_window_flags)) {
        open_ = window_open;
        ImGui::End();
        if (was_open && !open_) {
            imgui_system.discardUnsavedThemePreview();
        }
        return;
    }
    open_ = window_open;

    ImGui::BeginChild("SettingsSections", ImVec2(180.0F, 0.0F), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    if (ImGui::Selectable("Viewport config", selected_section_ == kViewportConfigSection)) {
        selected_section_ = kViewportConfigSection;
    }
    if (ImGui::Selectable("Graphics quality", selected_section_ == kGraphicsQualitySection)) {
        selected_section_ = kGraphicsQualitySection;
    }
    if (ImGui::Selectable("File import", selected_section_ == kFileImportSection)) {
        selected_section_ = kFileImportSection;
    }
    if (ImGui::Selectable("Appearance", selected_section_ == kAppearanceSection)) {
        selected_section_ = kAppearanceSection;
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("SettingsContent", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders);
    if (selected_section_ == kAppearanceSection) {
        ImGui::TextUnformatted("Appearance");
        ImGui::Separator();
        ImGui::InputTextWithHint("##ThemeSearch", "Search themes", theme_search_, IM_ARRAYSIZE(theme_search_));
        ImGui::Spacing();

        float button_area_height = ImGui::GetFrameHeightWithSpacing() * 2.5F;
        if (ImGui::BeginChild("AppearanceThemeList", ImVec2(0.0F, -button_area_height), ImGuiChildFlags_Borders)) {
            const float row_height = std::max(ImGui::GetTextLineHeightWithSpacing(), kThemeSwatchSize) + 6.0F;
            std::size_t visible_theme_count = 0;
            for (const ImGuiSystem::ThemeOption& theme : imgui_system.themes()) {
                if (!matchesSearch(theme, theme_search_)) {
                    continue;
                }

                ++visible_theme_count;
                const bool selected = imgui_system.previewThemeId() == theme.id;
                const float row_width = std::max(ImGui::GetContentRegionAvail().x, 1.0F);
                ImGui::PushID(theme.id.c_str());
                if (ImGui::Selectable("##theme", selected, ImGuiSelectableFlags_None, ImVec2(row_width, row_height))) {
                    imgui_system.previewThemeById(theme.id);
                }
                drawThemeRow(theme, selected, row_height);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                    ImGui::SetTooltip("%s\n%s", theme.scheme.c_str(), theme.author.c_str());
                }
                ImGui::PopID();
            }

            if (visible_theme_count == 0) {
                ImGui::TextUnformatted("No themes match the current search.");
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Text("Saved: %s", themeSchemeById(imgui_system, imgui_system.savedThemeId()));
        ImGui::SameLine();
        ImGui::Text("Preview: %s", themeSchemeById(imgui_system, imgui_system.previewThemeId()));

        const bool has_unsaved_preview = imgui_system.hasUnsavedThemePreview();
        ImGui::BeginDisabled(!has_unsaved_preview);
        if (ImGui::Button("Save")) {
            if (imgui_system.savePreviewTheme()) {
                appendSettingsLog(actions, "Theme saved as " + std::string(themeSchemeById(imgui_system, imgui_system.savedThemeId())) + ".");
            } else {
                appendSettingsLog(actions, "Failed to save the selected theme.");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert")) {
            if (imgui_system.revertToSavedTheme()) {
                appendSettingsLog(actions, "Theme preview reverted to " + std::string(themeSchemeById(imgui_system, imgui_system.savedThemeId())) + ".");
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Random")) {
            if (previewRandomTheme(imgui_system)) {
                appendSettingsLog(actions, "Theme preview randomized to " + std::string(themeSchemeById(imgui_system, imgui_system.previewThemeId())) + ".");
            }
        }
    } else if (selected_section_ == kViewportConfigSection) {
        ImGui::TextUnformatted("Viewport config");
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

        ImGui::Spacing();
        ImGui::TextUnformatted("Viewport config");
        ImGui::Separator();
        int selected_up_axis = default_file_import_settings.up_axis == UpAxis::Y ? 0 : 1;
        constexpr const char* up_axis_options[] = {"Y", "Z"};
        if (ImGui::Combo("Default up axis", &selected_up_axis, up_axis_options, IM_ARRAYSIZE(up_axis_options))) {
            default_file_import_settings.up_axis = selected_up_axis == 0 ? UpAxis::Y : UpAxis::Z;
            if (actions != nullptr) {
                actions->event_logs.push_back(EditorUiLogEvent{
                    .origin = "SETTINGS",
                    .message = std::string("Default up axis set to ") + mesh::upAxisName(default_file_import_settings.up_axis) + ".",
                });
            }
        }
    } else if (selected_section_ == kGraphicsQualitySection) {
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
        if (ImGui::Checkbox("Debug Performance", &graphics_quality_settings.debug_performance) && actions != nullptr) {
            actions->event_logs.push_back(EditorUiLogEvent{
                .origin = "SETTINGS",
                .message = std::string("Debug Performance ") +
                    (graphics_quality_settings.debug_performance ? "enabled." : "disabled."),
            });
        }
    } else if (selected_section_ == kFileImportSection) {
        ImGui::TextUnformatted("File import");
        ImGui::Separator();

        ImGui::Spacing();
        ImGui::SeparatorText("Common conventions");
        ImGui::TextWrapped("Different formats and tools use different axis conventions. New projects start from the default up axis in Viewport config, but each project can override it in the inspector.");
        ImGui::Spacing();
        ImGui::BulletText("OBJ: commonly Y-up");
        ImGui::BulletText("STL: no standard up axis, often treated as Z-up for CAD/manufacturing workflows");
        ImGui::BulletText("glTF: Y-up");
        ImGui::BulletText("FBX: often Y-up, but many DCC pipelines also use Z-up");
        ImGui::BulletText("Blender scenes: typically Z-up");
    }
    ImGui::EndChild();

    ImGui::End();

    if (was_open && !open_) {
        imgui_system.discardUnsavedThemePreview();
    }
}

}  // namespace meshtools::ui
