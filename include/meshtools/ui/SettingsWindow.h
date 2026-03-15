#pragma once

#include "meshtools/ui/EditorUiTypes.h"
#include "meshtools/ui/ImGuiSystem.h"

namespace meshtools::ui {

class SettingsWindow {
  public:
    void open();
    void draw(
        ViewportControlSettings& viewport_control_settings,
        GraphicsQualitySettings& graphics_quality_settings,
        FileImportSettings& default_file_import_settings,
        ImGuiSystem& imgui_system,
        EditorUiActions* actions
    );

  private:
    bool open_ = false;
    int selected_section_ = 0;
    char theme_search_[128] = "";
};

}  // namespace meshtools::ui
