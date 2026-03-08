#include "meshtools/ui/EditorUi.h"

#include <algorithm>
#include <cstdint>
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

bool isCmdOrCtrlHeld(const ImGuiIO& io) {
#if defined(__APPLE__)
    return io.KeySuper;
#else
    return io.KeyCtrl;
#endif
}

}  // namespace

EditorUi::EditorUi(GLFWwindow* window, const char* glsl_version) {
    window_ = window;

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

void EditorUi::setViewportTexture(std::uint32_t texture_id) {
    viewport_texture_id_ = texture_id;
}

EditorUiActions EditorUi::draw(const EditorUiState& state) {
    EditorUiActions actions;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 toolbar_position = viewport->WorkPos;
    const ImVec2 toolbar_size = ImVec2(viewport->WorkSize.x, kToolbarHeight);
    const ImVec2 dockspace_position = ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + kToolbarHeight);
    const ImVec2 dockspace_size = ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - kToolbarHeight);

    ImGui::SetNextWindowPos(toolbar_position);
    ImGui::SetNextWindowSize(toolbar_size);
    ImGui::SetNextWindowViewport(viewport->ID);
    drawToolbar(&actions);

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

    drawLeftPane(state);
    drawBottomPane(state);
    drawViewportPane(&actions);

    if (show_demo_window_) {
        ImGui::ShowDemoWindow(&show_demo_window_);
    }

    drawSettingsWindow();

    return actions;
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

void EditorUi::drawToolbar(EditorUiActions* actions) {
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
    if (actions != nullptr && ImGui::Button("Open")) {
        actions->request_open_mesh = true;
    } else if (actions == nullptr) {
        ImGui::Button("Open");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
    }
    ImGui::SameLine();
    if (ImGui::Button("Settings")) {
        settings_window_open_ = true;
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
    if (actions != nullptr && ImGui::Button("Exit", ImVec2(exit_button_width, 0.0F))) {
        actions->request_exit = true;
    }

    ImGui::End();
}

void EditorUi::drawLeftPane(const EditorUiState& state) {
    constexpr ImGuiWindowFlags pane_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove;

    ImGui::Begin(kLeftPaneWindowName, nullptr, pane_flags);
    ImGui::TextUnformatted("Scene");
    ImGui::Separator();
    if (state.active_document != nullptr) {
        ImGui::Selectable(state.active_document->displayName().c_str(), true);
    } else {
        ImGui::TextDisabled("No mesh loaded");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Inspector");
    if (state.active_document != nullptr) {
        const mesh::MeshDocument& document = *state.active_document;
        ImGui::Text("Path: %s", document.source_path.string().c_str());
        ImGui::Text("Format: %s", document.formatLabel().c_str());
        ImGui::Text("Vertices: %zu", document.positions.size());
        ImGui::Text("Triangles: %zu", document.triangles.size());
        ImGui::Text("Normals: %zu", document.normals.size());

        if (document.bounds.valid) {
            ImGui::SeparatorText("Bounds");
            ImGui::Text("Min: %.3f %.3f %.3f", document.bounds.minimum.x, document.bounds.minimum.y, document.bounds.minimum.z);
            ImGui::Text("Max: %.3f %.3f %.3f", document.bounds.maximum.x, document.bounds.maximum.y, document.bounds.maximum.z);
        }
    } else {
        ImGui::TextWrapped("Use the Open button to load an OBJ or STL mesh.");
    }

    ImGui::Checkbox("Show ImGui demo window", &show_demo_window_);
    ImGui::ColorEdit3("Clear color", reinterpret_cast<float*>(&clear_color_));
    ImGui::End();
}

void EditorUi::drawBottomPane(const EditorUiState& state) {
    constexpr ImGuiWindowFlags pane_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove;

    ImGui::Begin(kBottomPaneWindowName, nullptr, pane_flags);
    if (ImGui::BeginTabBar("BottomTabs")) {
        if (ImGui::BeginTabItem("Console")) {
            if (state.log_messages.empty()) {
                ImGui::TextDisabled("No log messages.");
            } else {
                for (const std::string& log_message : state.log_messages) {
                    ImGui::TextWrapped("%s", log_message.c_str());
                }
            }
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
            if (state.active_document != nullptr) {
                ImGui::Text("Active mesh: %s", state.active_document->displayName().c_str());
            } else {
                ImGui::TextUnformatted("No active selection.");
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::End();
}

void EditorUi::drawViewportPane(EditorUiActions* actions) {
    constexpr ImGuiWindowFlags pane_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove;

    ImGui::Begin(kViewportWindowName, nullptr, pane_flags);
    viewport_render_size_ = ImGui::GetContentRegionAvail();
    viewport_render_size_.x = std::max(viewport_render_size_.x, 1.0F);
    viewport_render_size_.y = std::max(viewport_render_size_.y, 1.0F);

    if (window_ != nullptr) {
        int window_width = 0;
        int window_height = 0;
        int framebuffer_width = 0;
        int framebuffer_height = 0;
        glfwGetWindowSize(window_, &window_width, &window_height);
        glfwGetFramebufferSize(window_, &framebuffer_width, &framebuffer_height);

        const float scale_x = window_width > 0 ? static_cast<float>(framebuffer_width) / static_cast<float>(window_width) : 1.0F;
        const float scale_y = window_height > 0 ? static_cast<float>(framebuffer_height) / static_cast<float>(window_height) : 1.0F;
        viewport_framebuffer_scale_ = ImVec2(std::max(scale_x, 1.0F), std::max(scale_y, 1.0F));
    } else {
        viewport_framebuffer_scale_ = ImVec2(1.0F, 1.0F);
    }

    if (viewport_texture_id_ != 0) {
        ImGui::Image(
            static_cast<ImTextureID>(viewport_texture_id_),
            viewport_render_size_,
            ImVec2(0.0F, 1.0F),
            ImVec2(1.0F, 0.0F)
        );
    }

    if (actions != nullptr && ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0F) {
            const float zoom_direction = viewport_control_settings_.invert_zoom ? -1.0F : 1.0F;
            actions->viewport_camera.zoom_delta += io.MouseWheel * zoom_direction;
        }

        const bool pan_with_modifier =
            (viewport_control_settings_.pan_modifier == PanModifier::Shift && io.KeyShift) ||
            (viewport_control_settings_.pan_modifier == PanModifier::CmdOrCtrl && isCmdOrCtrlHeld(io));
        const bool pan_with_right_click_only =
            viewport_control_settings_.pan_modifier == PanModifier::RightClick;

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) && (pan_with_modifier || pan_with_right_click_only)) {
            actions->viewport_camera.pan_delta.x += io.MouseDelta.x;
            actions->viewport_camera.pan_delta.y += io.MouseDelta.y;
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            actions->viewport_camera.orbit_delta.x += io.MouseDelta.x;
            const float y_direction = viewport_control_settings_.invert_y_movement ? -1.0F : 1.0F;
            actions->viewport_camera.orbit_delta.y += io.MouseDelta.y * y_direction;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            actions->viewport_camera.reset = true;
        }
    }
    ImGui::End();
}

void EditorUi::drawSettingsWindow() {
    if (!settings_window_open_) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(720.0F, 420.0F), ImGuiCond_FirstUseEver);

    constexpr ImGuiWindowFlags settings_window_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking;

    if (!ImGui::Begin("Settings", &settings_window_open_, settings_window_flags)) {
        ImGui::End();
        return;
    }

    ImGui::BeginChild("SettingsSections", ImVec2(180.0F, 0.0F), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    if (ImGui::Selectable("Viewport controls", selected_settings_section_ == 0)) {
        selected_settings_section_ = 0;
    }
    if (ImGui::Selectable("Graphics quality", selected_settings_section_ == 1)) {
        selected_settings_section_ = 1;
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("SettingsContent", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders);
    if (selected_settings_section_ == 0) {
        ImGui::TextUnformatted("Viewport controls");
        ImGui::Separator();
        ImGui::Checkbox("Invert Y movement", &viewport_control_settings_.invert_y_movement);
        ImGui::Checkbox("Invert zoom", &viewport_control_settings_.invert_zoom);

        int selected_pan_modifier = static_cast<int>(viewport_control_settings_.pan_modifier);
        constexpr const char* pan_modifier_options[] = {"Cmd/Ctrl", "Shift", "Right-click"};
        if (ImGui::Combo("Pan modifier", &selected_pan_modifier, pan_modifier_options, IM_ARRAYSIZE(pan_modifier_options))) {
            viewport_control_settings_.pan_modifier = static_cast<PanModifier>(selected_pan_modifier);
        }
    } else if (selected_settings_section_ == 1) {
        ImGui::TextUnformatted("Graphics quality");
        ImGui::Separator();
        ImGui::SliderInt(
            "Render resolution",
            &graphics_quality_settings_.render_resolution_percent,
            25,
            100,
            "%d%%"
        );
    }
    ImGui::EndChild();

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

ImVec2 EditorUi::viewportRenderSize() const {
    return viewport_render_size_;
}

ImVec2 EditorUi::viewportRenderTargetSize() const {
    const float scale = static_cast<float>(graphics_quality_settings_.render_resolution_percent) / 100.0F;
    return ImVec2(
        std::max(viewport_render_size_.x * viewport_framebuffer_scale_.x * scale, 1.0F),
        std::max(viewport_render_size_.y * viewport_framebuffer_scale_.y * scale, 1.0F)
    );
}

}  // namespace meshtools::ui
