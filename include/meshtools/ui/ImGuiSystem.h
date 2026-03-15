#pragma once

#include <array>
#include <string>
#include <vector>

#include "imgui.h"
#include "meshtools/render/ViewportRenderer.h"

struct GLFWwindow;

namespace meshtools::ui {

class ImGuiSystem {
  public:
    struct ThemeOption {
        std::string id;
        std::string scheme;
        std::string author;
        std::array<ImVec4, 16> base_colors{};
        bool is_builtin = false;
    };

    ImGuiSystem(GLFWwindow* window, const char* glsl_version);
    ~ImGuiSystem();

    ImGuiSystem(const ImGuiSystem&) = delete;
    ImGuiSystem& operator=(const ImGuiSystem&) = delete;

    void beginFrame() const;
    void endFrame(GLFWwindow* window) const;
    [[nodiscard]] const std::vector<ThemeOption>& themes() const;
    [[nodiscard]] const std::string& previewThemeId() const;
    [[nodiscard]] const std::string& savedThemeId() const;
    [[nodiscard]] bool hasUnsavedThemePreview() const;
    [[nodiscard]] const ImVec4& clearColor() const;
    [[nodiscard]] const render::ViewportRenderer::ThemeColors& rendererThemeColors() const;
    bool previewThemeById(const std::string& theme_id);
    bool savePreviewTheme();
    bool revertToSavedTheme();
    void discardUnsavedThemePreview();

  private:
    std::vector<ThemeOption> themes_{};
    std::string preview_theme_id_ = "imgui";
    std::string saved_theme_id_ = "imgui";
    ImVec4 clear_color_ = ImVec4(0.10F, 0.12F, 0.15F, 1.00F);
    render::ViewportRenderer::ThemeColors renderer_theme_colors_{};
};

}  // namespace meshtools::ui
