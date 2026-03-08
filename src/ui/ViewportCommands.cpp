#include "meshtools/ui/ViewportCommands.h"

namespace meshtools::ui {

void toggleWireframe(ViewportDisplaySettings& viewport_display_settings, EditorUiActions* actions) {
    viewport_display_settings.show_wireframe = !viewport_display_settings.show_wireframe;
    if (actions != nullptr) {
        actions->event_logs.push_back(EditorUiLogEvent{
            .origin = "VIEWPORT",
            .message = std::string("Show wireframe ") +
                (viewport_display_settings.show_wireframe ? "enabled." : "disabled."),
        });
    }
}

void toggleShadeTriangles(ViewportDisplaySettings& viewport_display_settings, EditorUiActions* actions) {
    viewport_display_settings.shade_triangles = !viewport_display_settings.shade_triangles;
    if (actions != nullptr) {
        actions->event_logs.push_back(EditorUiLogEvent{
            .origin = "VIEWPORT",
            .message = std::string("Shade triangles ") +
                (viewport_display_settings.shade_triangles ? "enabled." : "disabled."),
        });
    }
}

void resetViewport(
    ViewportDisplaySettings& viewport_display_settings,
    SelectionFilter& selection_filter,
    EditorUiActions* actions
) {
    viewport_display_settings.show_wireframe = true;
    viewport_display_settings.shade_triangles = true;
    selection_filter = SelectionFilter::Edges;

    if (actions != nullptr) {
        actions->viewport_camera.reset = true;
        actions->event_logs.push_back(EditorUiLogEvent{
            .origin = "VIEWPORT",
            .message = "Viewport reset.",
        });
    }
}

}  // namespace meshtools::ui
