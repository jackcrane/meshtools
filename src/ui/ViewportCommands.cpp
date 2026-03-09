#include "meshtools/ui/ViewportCommands.h"

namespace meshtools::ui {

namespace {

void logSelectionState(const char* label, bool enabled, EditorUiActions* actions) {
    if (actions == nullptr) {
        return;
    }

    actions->event_logs.push_back(EditorUiLogEvent{
        .origin = "VIEWPORT",
        .message = std::string(label) + (enabled ? " enabled." : " disabled."),
    });
}

}  // namespace

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

void toggleShowPoints(ViewportDisplaySettings& viewport_display_settings, EditorUiActions* actions) {
    viewport_display_settings.show_points = !viewport_display_settings.show_points;
    if (actions != nullptr) {
        actions->event_logs.push_back(EditorUiLogEvent{
            .origin = "VIEWPORT",
            .message = std::string("Show points ") +
                (viewport_display_settings.show_points ? "enabled." : "disabled."),
        });
    }
}

void toggleEdgeSelection(SelectionFilters& selection_filters, EditorUiActions* actions) {
    selection_filters.edges = !selection_filters.edges;
    logSelectionState("Edge selection", selection_filters.edges, actions);
}

void toggleFaceSelection(SelectionFilters& selection_filters, EditorUiActions* actions) {
    selection_filters.faces = !selection_filters.faces;
    logSelectionState("Face selection", selection_filters.faces, actions);
}

void togglePointSelection(SelectionFilters& selection_filters, EditorUiActions* actions) {
    selection_filters.points = !selection_filters.points;
    logSelectionState("Point selection", selection_filters.points, actions);
}

void requestInvertSelection(EditorUiActions* actions) {
    if (actions == nullptr) {
        return;
    }

    actions->request_invert_selection = true;
}

void resetViewport(
    ViewportDisplaySettings& viewport_display_settings,
    SelectionFilters& selection_filters,
    EditorUiActions* actions
) {
    viewport_display_settings.show_wireframe = true;
    viewport_display_settings.shade_triangles = true;
    viewport_display_settings.show_points = false;
    selection_filters = SelectionFilters{};

    if (actions != nullptr) {
        actions->viewport_camera.reset = true;
        actions->event_logs.push_back(EditorUiLogEvent{
            .origin = "VIEWPORT",
            .message = "Viewport reset.",
        });
    }
}

}  // namespace meshtools::ui
