#include "meshtools/ui/ImGuiSystem.h"

#include <array>
#include <filesystem>
#include <stdexcept>

#include <GLFW/glfw3.h>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

namespace meshtools::ui {
namespace {

void mergeSymbolFont(ImGuiIO& io) {
    const std::array<const char*, 2> candidate_paths = {
        "/System/Library/Fonts/Apple Symbols.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    };
    constexpr ImWchar glyph_ranges[] = {
        0x2503, 0x2503,  // ┃
        0x2588, 0x2588,  // █
        0x25C6, 0x25C7,  // ◆ ◇
        0x2737, 0x2737,  // ✷
        0,
    };

    ImFontConfig config;
    config.MergeMode = true;
    config.PixelSnapH = true;

    for (const char* path : candidate_paths) {
        if (std::filesystem::exists(path) && io.Fonts->AddFontFromFileTTF(path, 15.0F, &config, glyph_ranges) != nullptr) {
            return;
        }
    }
}

}  // namespace

ImGuiSystem::ImGuiSystem(GLFWwindow* window, const char* glsl_version) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.Fonts->AddFontDefault();
    mergeSymbolFont(io);

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        style.WindowRounding = 0.0F;
        style.Colors[ImGuiCol_WindowBg].w = 1.0F;
    }

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        ImGui::DestroyContext();
        throw std::runtime_error("Failed to initialize ImGui GLFW backend.");
    }

    if (!ImGui_ImplOpenGL3_Init(glsl_version)) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("Failed to initialize ImGui OpenGL backend.");
    }
}

ImGuiSystem::~ImGuiSystem() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiSystem::beginFrame() const {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiSystem::endFrame(GLFWwindow* window) const {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        GLFWwindow* backup_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_context != nullptr ? backup_context : window);
    }
}

}  // namespace meshtools::ui
