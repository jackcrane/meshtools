#pragma once

#include "meshtools/ui/EditorUiTypes.h"

namespace meshtools::ui {

void toggleWireframe(ViewportDisplaySettings& viewport_display_settings, EditorUiActions* actions);
void toggleShadeTriangles(ViewportDisplaySettings& viewport_display_settings, EditorUiActions* actions);
void toggleEdgeSelection(SelectionFilters& selection_filters, EditorUiActions* actions);
void toggleFaceSelection(SelectionFilters& selection_filters, EditorUiActions* actions);
void togglePointSelection(SelectionFilters& selection_filters, EditorUiActions* actions);
void resetViewport(
    ViewportDisplaySettings& viewport_display_settings,
    SelectionFilters& selection_filters,
    EditorUiActions* actions
);

}  // namespace meshtools::ui
