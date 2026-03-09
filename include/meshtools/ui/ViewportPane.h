#pragma once

#include <functional>
#include <span>
#include <string_view>

#include "meshtools/ui/EditorUiTypes.h"

struct GLFWwindow;

namespace meshtools::ui {

class ViewportPane {
  public:
    explicit ViewportPane(GLFWwindow* window);

    void draw(
        const EditorUiState& state,
        const ViewportControlSettings& viewport_control_settings,
        const FileImportSettings& default_file_import_settings,
        ViewportDisplaySettings& viewport_display_settings,
        SelectionFilters& selection_filters,
        std::string_view wireframe_shortcut,
        std::string_view shade_triangles_shortcut,
        std::string_view show_points_shortcut,
        std::string_view edge_shortcut,
        std::string_view face_shortcut,
        std::string_view point_shortcut,
        EditorUiActions* actions,
        const std::function<void()>& on_toggle_wireframe,
        const std::function<void()>& on_toggle_shade_triangles,
        const std::function<void()>& on_toggle_show_points,
        const std::function<void()>& on_toggle_edges,
        const std::function<void()>& on_toggle_faces,
        const std::function<void()>& on_toggle_points
    );

    void setTexture(std::uint32_t texture_id);
    [[nodiscard]] ImVec2 renderSize() const;
    [[nodiscard]] ImVec2 renderTargetSize(const GraphicsQualitySettings& graphics_quality_settings) const;

  private:
    struct DragSelectionState {
        bool active = false;
        bool toggle_existing = false;
        ImVec2 start = ImVec2(0.0F, 0.0F);
        ImVec2 current = ImVec2(0.0F, 0.0F);
    };

    struct SegmentedControlItem {
        const char* label = "";
        const char* tooltip = "";
        std::string_view shortcut;
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
    DragSelectionState drag_selection_{};
};

}  // namespace meshtools::ui
