#pragma once

#include "meshtools/ui/BottomPane.h"
#include "meshtools/render/ViewportRenderer.h"
#include "meshtools/ui/EditorDockLayout.h"
#include "meshtools/ui/EditorUiTypes.h"
#include "meshtools/ui/ImGuiSystem.h"
#include "meshtools/ui/LeftPane.h"
#include "meshtools/ui/SequentialShortcutController.h"
#include "meshtools/ui/SettingsWindow.h"
#include "meshtools/ui/ViewportPane.h"

struct GLFWwindow;

namespace meshtools::ui {

class EditorUi {
  public:
    EditorUi(GLFWwindow* window, const char* glsl_version);
    ~EditorUi();

    EditorUi(const EditorUi&) = delete;
    EditorUi& operator=(const EditorUi&) = delete;

    void beginFrame() const;
    [[nodiscard]] EditorUiActions draw(const EditorUiState& state);
    void endFrame(GLFWwindow* window) const;
    void openSettingsWindow();
    void setViewportTexture(std::uint32_t texture_id);

    [[nodiscard]] const ImVec4& clearColor() const;
    [[nodiscard]] const FileImportSettings& fileImportSettings() const;
    [[nodiscard]] const SelectionFilters& selectionFilters() const;
    [[nodiscard]] const ViewportDisplaySettings& viewportDisplaySettings() const;
    [[nodiscard]] const render::ViewportRenderer::ThemeColors& viewportThemeColors() const;
    [[nodiscard]] ImVec2 viewportRenderSize() const;
    [[nodiscard]] ImVec2 viewportRenderTargetSize() const;

  private:
    void handleGlobalShortcuts(EditorUiActions* actions) const;
    void triggerShortcutAction(ShortcutCommand action, const EditorUiState& state, EditorUiActions* actions);
    SelectionFilters selection_filters_{};
    ViewportControlSettings viewport_control_settings_{};
    GraphicsQualitySettings graphics_quality_settings_{};
    FileImportSettings file_import_settings_{};
    ViewportDisplaySettings viewport_display_settings_{};
    ImGuiSystem imgui_system_;
    EditorDockLayout dock_layout_;
    LeftPane left_pane_;
    BottomPane bottom_pane_;
    ViewportPane viewport_pane_;
    SettingsWindow settings_window_;
    SequentialShortcutController sequential_shortcuts_;
};

}  // namespace meshtools::ui
