#pragma once

#include "imgui.h"

struct GLFWwindow;

namespace meshtools::ui {

class EditorUi {
  public:
    EditorUi(GLFWwindow* window, const char* glsl_version);
    ~EditorUi();

    EditorUi(const EditorUi&) = delete;
    EditorUi& operator=(const EditorUi&) = delete;

    void beginFrame() const;
    void draw(bool* request_exit);
    void endFrame(GLFWwindow* window) const;

    [[nodiscard]] const ImVec4& clearColor() const;

  private:
    void buildDefaultLayout(ImGuiID dockspace_id, const ImVec2& dockspace_size);
    void drawToolbar(bool* request_exit);
    void drawLeftPane();
    void drawBottomPane();
    void drawViewportPane();

    bool layout_initialized_ = false;
    bool show_demo_window_ = true;
    ImVec4 clear_color_ = ImVec4(0.10F, 0.12F, 0.15F, 1.00F);
};

}  // namespace meshtools::ui
