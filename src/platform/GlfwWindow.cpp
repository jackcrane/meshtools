#include "meshtools/platform/GlfwWindow.h"

#include <iostream>
#include <stdexcept>
#include <utility>

#include <GLFW/glfw3.h>

namespace meshtools::platform {
namespace {

void glfwErrorCallback(int code, const char* description) {
    std::cerr << "GLFW error (" << code << "): " << description << '\n';
}

}  // namespace

GlfwWindow::GlfwWindow(const WindowConfig& config) {
    glfwSetErrorCallback(glfwErrorCallback);

    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("Failed to initialize GLFW.");
    }

#if defined(__APPLE__)
    glsl_version_ = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    glsl_version_ = "#version 330";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif

    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    handle_ = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
    if (handle_ == nullptr) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window.");
    }

    glfwMakeContextCurrent(handle_);
    glfwSwapInterval(1);
}

GlfwWindow::~GlfwWindow() {
    destroy();
}

GlfwWindow::GlfwWindow(GlfwWindow&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      glsl_version_(std::move(other.glsl_version_)) {}

GlfwWindow& GlfwWindow::operator=(GlfwWindow&& other) noexcept {
    if (this != &other) {
        destroy();
        handle_ = std::exchange(other.handle_, nullptr);
        glsl_version_ = std::move(other.glsl_version_);
    }
    return *this;
}

void GlfwWindow::beginFrame(float red, float green, float blue, float alpha) const {
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    glfwGetFramebufferSize(handle_, &framebuffer_width, &framebuffer_height);

    glViewport(0, 0, framebuffer_width, framebuffer_height);
    glClearColor(red, green, blue, alpha);
    glClear(GL_COLOR_BUFFER_BIT);
}

void GlfwWindow::pollEvents() const {
    glfwPollEvents();
}

void GlfwWindow::swapBuffers() const {
    glfwSwapBuffers(handle_);
}

bool GlfwWindow::shouldClose() const {
    return glfwWindowShouldClose(handle_) == GLFW_TRUE;
}

void GlfwWindow::requestClose() const {
    glfwSetWindowShouldClose(handle_, GLFW_TRUE);
}

GLFWwindow* GlfwWindow::nativeHandle() const {
    return handle_;
}

const char* GlfwWindow::glslVersion() const {
    return glsl_version_.c_str();
}

void GlfwWindow::destroy() {
    if (handle_ != nullptr) {
        glfwDestroyWindow(handle_);
        handle_ = nullptr;
        glfwTerminate();
    }
}

}  // namespace meshtools::platform
