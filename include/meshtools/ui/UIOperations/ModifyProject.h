#pragma once

#include <array>
#include <optional>
#include <string>

#include "meshtools/ui/EditorUiTypes.h"

namespace meshtools::ui {

class ModifyProjectOperation {
  public:
    void openDialog();
    void draw(const EditorUiState& state, EditorUiActions* actions);

  private:
    struct EndpointSlot {
        std::optional<mesh::ModifyProjectTarget> target;
    };

    bool dialog_open_ = false;
    bool dialog_pending_open_ = false;
    std::array<std::uint32_t, 2> source_faces_ = {0, 0};
    bool infinite_length_ = true;
    EndpointSlot start_{};
    EndpointSlot end_{};
    std::string capture_feedback_;
};

}  // namespace meshtools::ui
