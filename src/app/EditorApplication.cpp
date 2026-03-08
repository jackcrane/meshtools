#include "meshtools/app/EditorApplication.h"

#include <utility>

namespace meshtools::app {

EditorApplication::EditorApplication(AppConfig config)
    : config_(std::move(config)),
      window_(platform::WindowConfig{.title = config_.name, .width = config_.width, .height = config_.height}),
      editor_ui_(window_.nativeHandle(), window_.glslVersion()) {}

int EditorApplication::run() {
    while (!window_.shouldClose()) {
        window_.pollEvents();
        const ImVec4& clear_color = editor_ui_.clearColor();
        window_.beginFrame(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        editor_ui_.beginFrame();

        bool request_exit = false;
        editor_ui_.draw(&request_exit);
        editor_ui_.endFrame(window_.nativeHandle());
        window_.swapBuffers();

        if (request_exit) {
            window_.requestClose();
        }
    }

    return 0;
}

}  // namespace meshtools::app
