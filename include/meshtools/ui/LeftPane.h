#pragma once

#include "meshtools/ui/EditorUiTypes.h"

namespace meshtools::ui {

class LeftPane {
  public:
    void draw(const EditorUiState& state, EditorUiActions* actions) const;
};

}  // namespace meshtools::ui
