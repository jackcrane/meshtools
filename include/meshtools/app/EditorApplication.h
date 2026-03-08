#pragma once

#include <string>

#include "meshtools/platform/GlfwWindow.h"
#include "meshtools/ui/EditorUi.h"

namespace meshtools::app {

struct AppConfig {
    std::string name = "MeshTools";
    int width = 1600;
    int height = 900;
};

class EditorApplication {
  public:
    explicit EditorApplication(AppConfig config = {});

    int run();

  private:
    AppConfig config_;
    platform::GlfwWindow window_;
    ui::EditorUi editor_ui_;
};

}  // namespace meshtools::app

