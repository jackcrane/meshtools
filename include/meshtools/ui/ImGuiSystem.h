#pragma once

struct GLFWwindow;

namespace meshtools::ui {

class ImGuiSystem {
  public:
    ImGuiSystem(GLFWwindow* window, const char* glsl_version);
    ~ImGuiSystem();

    ImGuiSystem(const ImGuiSystem&) = delete;
    ImGuiSystem& operator=(const ImGuiSystem&) = delete;

    void beginFrame() const;
    void endFrame(GLFWwindow* window) const;
};

}  // namespace meshtools::ui
