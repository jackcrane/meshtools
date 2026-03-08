#pragma once

#include "meshtools/ui/EditorUiTypes.h"

namespace meshtools::ui {

class SettingsWindow {
  public:
    void open();
    void draw(
        ViewportControlSettings& viewport_control_settings,
        GraphicsQualitySettings& graphics_quality_settings,
        FileImportSettings& file_import_settings,
        EditorUiActions* actions
    );

  private:
    bool open_ = false;
    int selected_section_ = 0;
};

}  // namespace meshtools::ui
