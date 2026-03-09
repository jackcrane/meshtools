#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "meshtools/ui/EditorUiTypes.h"

namespace meshtools::ui {

class LeftPane {
  public:
    void draw(const EditorUiState& state, EditorUiActions* actions);

  private:
    void beginRenamingEntitySet(std::size_t index, std::string_view current_name);

    std::optional<std::size_t> renaming_entity_set_index_;
    std::array<char, 256> rename_buffer_{};
    bool focus_entity_set_rename_ = false;
};

}  // namespace meshtools::ui
