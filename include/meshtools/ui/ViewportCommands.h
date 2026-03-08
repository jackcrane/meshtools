#pragma once

#include "meshtools/ui/EditorUiTypes.h"

namespace meshtools::ui {

void toggleWireframe(ViewportDisplaySettings& viewport_display_settings, EditorUiActions* actions);
void toggleShadeTriangles(ViewportDisplaySettings& viewport_display_settings, EditorUiActions* actions);
void resetViewport(
    ViewportDisplaySettings& viewport_display_settings,
    SelectionFilter& selection_filter,
    EditorUiActions* actions
);

}  // namespace meshtools::ui
