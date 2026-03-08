#include "meshtools/ui/EditorUi.h"

#include <stdexcept>

#include <GLFW/glfw3.h>

#include "imgui_internal.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

namespace meshtools::ui {
namespace {

constexpr const char* kDockspaceWindowName = "MainDockspace";
constexpr const char* kDockspaceName = "EditorDockspace";
constexpr const char* kViewportWindowName = "Viewport";
constexpr const char* kLeftPaneWindowName = "Outliner";
constexpr const char* kBottomPaneWindowName = "Console";
constexpr float kToolbarHeight = 52.0F;

}  // namespace

EditorUi::EditorUi(GLFWwindow* window, const char* glsl_version) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

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

EditorUi::~EditorUi() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void EditorUi::beginFrame() const {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void EditorUi::draw(bool* request_exit) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 toolbar_position = viewport->WorkPos;
    const ImVec2 toolbar_size = ImVec2(viewport->WorkSize.x, kToolbarHeight);
    const ImVec2 dockspace_position = ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + kToolbarHeight);
    const ImVec2 dockspace_size = ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - kToolbarHeight);

    ImGui::SetNextWindowPos(toolbar_position);
    ImGui::SetNextWindowSize(toolbar_size);
    ImGui::SetNextWindowViewport(viewport->ID);
    drawToolbar(request_exit);

    ImGui::SetNextWindowPos(dockspace_position);
    ImGui::SetNextWindowSize(dockspace_size);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags dockspace_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    constexpr ImGuiDockNodeFlags docknode_flags = ImGuiDockNodeFlags_PassthruCentralNode;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

    ImGui::Begin(kDockspaceWindowName, nullptr, dockspace_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID(kDockspaceName);
    buildDefaultLayout(dockspace_id, dockspace_size);
    ImGui::DockSpace(dockspace_id, ImVec2(0.0F, 0.0F), docknode_flags);
    ImGui::End();

    drawLeftPane();
    drawBottomPane();
    drawViewportPane();

    if (show_demo_window_) {
        ImGui::ShowDemoWindow(&show_demo_window_);
    }
}

void EditorUi::buildDefaultLayout(ImGuiID dockspace_id, const ImVec2& dockspace_size) {
    if (layout_initialized_) {
        return;
    }

    layout_initialized_ = true;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, dockspace_size);

    ImGuiID main_dock_id = dockspace_id;
    ImGuiID left_dock_id = ImGui::DockBuilderSplitNode(main_dock_id, ImGuiDir_Left, 0.24F, nullptr, &main_dock_id);
    ImGuiID bottom_dock_id = ImGui::DockBuilderSplitNode(main_dock_id, ImGuiDir_Down, 0.26F, nullptr, &main_dock_id);

    ImGui::DockBuilderDockWindow(kLeftPaneWindowName, left_dock_id);
    ImGui::DockBuilderDockWindow(kBottomPaneWindowName, bottom_dock_id);
    ImGui::DockBuilderDockWindow(kViewportWindowName, main_dock_id);

    constexpr ImGuiDockNodeFlags locked_node_flags =
        ImGuiDockNodeFlags_NoDockingSplit |
        ImGuiDockNodeFlags_NoResize |
        ImGuiDockNodeFlags_NoUndocking |
        ImGuiDockNodeFlags_NoTabBar;

    if (ImGuiDockNode* left_node = ImGui::DockBuilderGetNode(left_dock_id); left_node != nullptr) {
        left_node->LocalFlags |= locked_node_flags;
    }
    if (ImGuiDockNode* bottom_node = ImGui::DockBuilderGetNode(bottom_dock_id); bottom_node != nullptr) {
        bottom_node->LocalFlags |= locked_node_flags;
    }
    if (ImGuiDockNode* main_node = ImGui::DockBuilderGetNode(main_dock_id); main_node != nullptr) {
        main_node->LocalFlags |= locked_node_flags;
    }

    ImGui::DockBuilderFinish(dockspace_id);
}

void EditorUi::drawToolbar(bool* request_exit) {
    constexpr ImGuiWindowFlags toolbar_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F, 10.0F));
    ImGui::Begin("Toolbar", nullptr, toolbar_flags);
    ImGui::PopStyleVar(3);

    if (ImGui::Button("New")) {
    }
    ImGui::SameLine();
    if (ImGui::Button("Open")) {
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
    }
    ImGui::SameLine();
    ImGui::TextDisabled("| Tools");
    ImGui::SameLine();
    ImGui::Button("Select");
    ImGui::SameLine();
    ImGui::Button("Move");
    ImGui::SameLine();
    ImGui::Button("Rotate");

    const float exit_button_width = 72.0F;
    ImGui::SameLine(ImGui::GetWindowWidth() - exit_button_width - 18.0F);
    if (request_exit != nullptr && ImGui::Button("Exit", ImVec2(exit_button_width, 0.0F))) {
        *request_exit = true;
    }

    ImGui::End();
}

void EditorUi::drawLeftPane() {
    constexpr ImGuiWindowFlags pane_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove;

    ImGui::Begin(kLeftPaneWindowName, nullptr, pane_flags);
    ImGui::TextUnformatted("Scene");
    ImGui::Separator();
    ImGui::BulletText("Root");
    ImGui::Indent();
    ImGui::Selectable("Mesh_001", true);
    ImGui::Selectable("Camera");
    ImGui::Selectable("DirectionalLight");
    ImGui::Unindent();

    ImGui::Spacing();
    ImGui::SeparatorText("Inspector");
    ImGui::TextWrapped("This pane is reserved for hierarchy, selection state, and object properties.");
    ImGui::Checkbox("Show ImGui demo window", &show_demo_window_);
    ImGui::ColorEdit3("Clear color", reinterpret_cast<float*>(&clear_color_));
    ImGui::End();
}

void EditorUi::drawBottomPane() {
    constexpr ImGuiWindowFlags pane_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove;

    ImGui::Begin(kBottomPaneWindowName, nullptr, pane_flags);
    if (ImGui::BeginTabBar("BottomTabs")) {
        if (ImGui::BeginTabItem("Console")) {
            ImGui::TextWrapped("[info] Editor shell initialized.");
            ImGui::TextWrapped("[info] Bottom panel is ready for logs, validation output, and command history.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stats")) {
            ImGuiIO& io = ImGui::GetIO();
            const float frame_time_ms = io.Framerate > 0.0F ? (1000.0F / io.Framerate) : 0.0F;
            ImGui::Text("Frame time: %.3f ms", frame_time_ms);
            ImGui::Text("FPS: %.1f", io.Framerate);
            ImGui::Text("Display size: %.0f x %.0f", io.DisplaySize.x, io.DisplaySize.y);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Selection")) {
            ImGui::TextUnformatted("No active selection.");
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::End();
}

void EditorUi::drawViewportPane() {
    constexpr ImGuiWindowFlags pane_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove;

    ImGui::Begin(kViewportWindowName, nullptr, pane_flags);
    ImGui::End();
}

void EditorUi::endFrame(GLFWwindow* window) const {
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

const ImVec4& EditorUi::clearColor() const {
    return clear_color_;
}

}  // namespace meshtools::ui
