#pragma once

#include <string>

struct GLFWwindow;

namespace meshtools::platform {

struct WindowConfig {
    std::string title;
    int width = 1280;
    int height = 720;
};

class GlfwWindow {
  public:
    explicit GlfwWindow(const WindowConfig& config);
    ~GlfwWindow();

    GlfwWindow(const GlfwWindow&) = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;

    GlfwWindow(GlfwWindow&& other) noexcept;
    GlfwWindow& operator=(GlfwWindow&& other) noexcept;

    void beginFrame(float red, float green, float blue, float alpha) const;
    void pollEvents() const;
    void swapBuffers() const;
    bool shouldClose() const;
    void requestClose() const;

    [[nodiscard]] GLFWwindow* nativeHandle() const;
    [[nodiscard]] const char* glslVersion() const;

  private:
    void destroy();

    GLFWwindow* handle_ = nullptr;
    std::string glsl_version_;
};

}  // namespace meshtools::platform

