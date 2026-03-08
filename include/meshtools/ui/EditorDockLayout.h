#pragma once

namespace meshtools::ui {

inline constexpr const char* kEditorDockspaceWindowName = "MainDockspace";
inline constexpr const char* kEditorDockspaceName = "EditorDockspace";
inline constexpr const char* kEditorViewportWindowName = "Viewport";
inline constexpr const char* kEditorLeftPaneWindowName = "Outliner";
inline constexpr const char* kEditorBottomPaneWindowName = "Console";

class EditorDockLayout {
  public:
    void draw();

  private:
    bool layout_initialized_ = false;
};

}  // namespace meshtools::ui
