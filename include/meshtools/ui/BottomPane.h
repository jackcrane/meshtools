#pragma once

#include <cstddef>

#include "meshtools/ui/EditorUiTypes.h"

namespace meshtools::ui {

class BottomPane {
  public:
    void draw(const EditorUiState& state);

  private:
    std::size_t last_console_log_count_ = 0;
};

}  // namespace meshtools::ui
