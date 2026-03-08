#pragma once

#include <span>
#include <string>

#include "meshtools/mesh/MeshDocument.h"
#include "imgui.h"

struct GLFWwindow;

namespace meshtools::ui {

struct EditorUiState {
    const mesh::MeshDocument* active_document = nullptr;
    std::span<const std::string> log_messages;
};

enum class PanModifier {
    CmdOrCtrl,
    Shift,
    RightClick,
};

struct ViewportControlSettings {
    bool invert_y_movement = true;
    bool invert_zoom = false;
    PanModifier pan_modifier = PanModifier::Shift;
};

struct GraphicsQualitySettings {
    int render_resolution_percent = 100;
};

struct ViewportCameraInput {
    ImVec2 orbit_delta = ImVec2(0.0F, 0.0F);
    ImVec2 pan_delta = ImVec2(0.0F, 0.0F);
    float zoom_delta = 0.0F;
    bool reset = false;
};

struct EditorUiActions {
    bool request_exit = false;
    bool request_open_mesh = false;
    ViewportCameraInput viewport_camera;
};

class EditorUi {
  public:
    EditorUi(GLFWwindow* window, const char* glsl_version);
    ~EditorUi();

    EditorUi(const EditorUi&) = delete;
    EditorUi& operator=(const EditorUi&) = delete;

    void beginFrame() const;
    [[nodiscard]] EditorUiActions draw(const EditorUiState& state);
    void endFrame(GLFWwindow* window) const;
    void setViewportTexture(std::uint32_t texture_id);

    [[nodiscard]] const ImVec4& clearColor() const;
    [[nodiscard]] ImVec2 viewportRenderSize() const;
    [[nodiscard]] ImVec2 viewportRenderTargetSize() const;

  private:
    void buildDefaultLayout(ImGuiID dockspace_id, const ImVec2& dockspace_size);
    void drawToolbar(EditorUiActions* actions);
    void drawLeftPane(const EditorUiState& state);
    void drawBottomPane(const EditorUiState& state);
    void drawViewportPane(EditorUiActions* actions);
    void drawSettingsWindow();

    bool layout_initialized_ = false;
    bool settings_window_open_ = false;
    bool show_demo_window_ = true;
    int selected_settings_section_ = 0;
    GLFWwindow* window_ = nullptr;
    ImVec4 clear_color_ = ImVec4(0.10F, 0.12F, 0.15F, 1.00F);
    ImVec2 viewport_render_size_ = ImVec2(1280.0F, 720.0F);
    ImVec2 viewport_framebuffer_scale_ = ImVec2(1.0F, 1.0F);
    std::uint32_t viewport_texture_id_ = 0;
    ViewportControlSettings viewport_control_settings_{};
    GraphicsQualitySettings graphics_quality_settings_{};
};

}  // namespace meshtools::ui
