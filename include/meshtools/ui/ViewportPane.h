#pragma once

#include <functional>
#include <span>

#include "meshtools/ui/EditorUiTypes.h"

struct GLFWwindow;

namespace meshtools::ui {

class ViewportPane {
  public:
    explicit ViewportPane(GLFWwindow* window);

    void draw(
        const EditorUiState& state,
        const ViewportControlSettings& viewport_control_settings,
        FileImportSettings& file_import_settings,
        ViewportDisplaySettings& viewport_display_settings,
        SelectionFilter& selection_filter,
        EditorUiActions* actions,
        const std::function<void()>& on_toggle_wireframe,
        const std::function<void()>& on_toggle_shade_triangles
    );

    void setTexture(std::uint32_t texture_id);
    [[nodiscard]] ImVec2 renderSize() const;
    [[nodiscard]] ImVec2 renderTargetSize(const GraphicsQualitySettings& graphics_quality_settings) const;

  private:
    struct SegmentedControlItem {
        const char* label = "";
        const char* tooltip = "";
        bool selected = false;
    };

    [[nodiscard]] int drawSegmentedControl(
        const char* id,
        const ImVec2& top_right,
        std::span<const SegmentedControlItem> items
    ) const;

    GLFWwindow* window_ = nullptr;
    ImVec2 render_size_ = ImVec2(1280.0F, 720.0F);
    ImVec2 framebuffer_scale_ = ImVec2(1.0F, 1.0F);
    std::uint32_t texture_id_ = 0;
};

}  // namespace meshtools::ui
