#pragma once

#include "meshtools/ui/EditorUiTypes.h"

namespace meshtools::ui {

class SelectSimilarOperation {
  public:
    void openDialog();
    void draw(const EditorUiState& state, EditorUiActions* actions);

  private:
    bool dialog_open_ = false;
    bool dialog_pending_open_ = false;
    render::SelectSimilarParams config_{};
};

}  // namespace meshtools::ui
