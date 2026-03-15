#include "meshtools/ui/ImGuiSystem.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string_view>

#include <GLFW/glfw3.h>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

namespace meshtools::ui {
namespace {

constexpr const char* kDefaultThemeId = "imgui";

std::string trim(std::string value) {
    const auto is_space = [](unsigned char character) {
        return std::isspace(character) != 0;
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&is_space](char character) {
                    return !is_space(static_cast<unsigned char>(character));
                }));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), [&is_space](char character) {
            return !is_space(static_cast<unsigned char>(character));
        }).base(),
        value.end()
    );
    return value;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string unquote(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::optional<ImVec4> parseHexColor(std::string value) {
    value = trim(std::move(value));
    if (!value.empty() && value.front() == '#') {
        value.erase(value.begin());
    }
    value = unquote(std::move(value));
    if (value.size() != 6) {
        return std::nullopt;
    }

    const auto parse_component = [](std::string_view component) -> std::optional<float> {
        unsigned int parsed = 0;
        for (char character : component) {
            parsed *= 16U;
            if (character >= '0' && character <= '9') {
                parsed += static_cast<unsigned int>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                parsed += static_cast<unsigned int>(character - 'a' + 10);
            } else if (character >= 'A' && character <= 'F') {
                parsed += static_cast<unsigned int>(character - 'A' + 10);
            } else {
                return std::nullopt;
            }
        }
        return static_cast<float>(parsed) / 255.0F;
    };

    const std::optional<float> red = parse_component(std::string_view(value).substr(0, 2));
    const std::optional<float> green = parse_component(std::string_view(value).substr(2, 2));
    const std::optional<float> blue = parse_component(std::string_view(value).substr(4, 2));
    if (!red.has_value() || !green.has_value() || !blue.has_value()) {
        return std::nullopt;
    }

    return ImVec4(*red, *green, *blue, 1.0F);
}

float luminance(const ImVec4& color) {
    return color.x * 0.2126F + color.y * 0.7152F + color.z * 0.0722F;
}

ImVec4 withAlpha(const ImVec4& color, float alpha) {
    return ImVec4(color.x, color.y, color.z, alpha);
}

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

ImGuiSystem::ThemeOption makeBuiltinTheme() {
    ImGuiSystem::ThemeOption theme;
    theme.id = kDefaultThemeId;
    theme.scheme = "ImGUI";
    theme.author = "Dear ImGui";
    theme.is_builtin = true;
    theme.base_colors = {
        ImVec4(0.10F, 0.10F, 0.10F, 1.0F),
        ImVec4(0.20F, 0.21F, 0.22F, 1.0F),
        ImVec4(0.24F, 0.29F, 0.35F, 1.0F),
        ImVec4(0.43F, 0.43F, 0.50F, 1.0F),
        ImVec4(0.35F, 0.40F, 0.47F, 1.0F),
        ImVec4(1.00F, 1.00F, 1.00F, 1.0F),
        ImVec4(0.90F, 0.90F, 0.90F, 1.0F),
        ImVec4(1.00F, 1.00F, 1.00F, 1.0F),
        ImVec4(0.90F, 0.39F, 0.39F, 1.0F),
        ImVec4(0.98F, 0.59F, 0.26F, 1.0F),
        ImVec4(0.97F, 0.82F, 0.28F, 1.0F),
        ImVec4(0.44F, 0.78F, 0.56F, 1.0F),
        ImVec4(0.36F, 0.80F, 0.84F, 1.0F),
        ImVec4(0.26F, 0.59F, 0.98F, 1.0F),
        ImVec4(0.74F, 0.56F, 0.96F, 1.0F),
        ImVec4(0.63F, 0.43F, 0.31F, 1.0F),
    };
    return theme;
}

void applyViewportStyleAdjustments() {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        style.WindowRounding = 0.0F;
        style.Colors[ImGuiCol_WindowBg].w = 1.0F;
    }
}

void applyBuiltinTheme(ImVec4* clear_color) {
    ImGui::StyleColorsDark();
    ImGui::GetStyle().Colors[ImGuiCol_PopupBg] = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    if (clear_color != nullptr) {
        *clear_color = ImVec4(0.10F, 0.12F, 0.15F, 1.00F);
    }
    applyViewportStyleAdjustments();
}

void applyBase16Theme(const ImGuiSystem::ThemeOption& theme, ImVec4* clear_color) {
    const bool dark_background = luminance(theme.base_colors[0]) < 0.5F;
    if (dark_background) {
        ImGui::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
    }

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4& base00 = theme.base_colors[0];
    const ImVec4& base01 = theme.base_colors[1];
    const ImVec4& base02 = theme.base_colors[2];
    const ImVec4& base03 = theme.base_colors[3];
    const ImVec4& base04 = theme.base_colors[4];
    const ImVec4& base05 = theme.base_colors[5];
    const ImVec4& base06 = theme.base_colors[6];
    const ImVec4& base07 = theme.base_colors[7];
    const ImVec4& base08 = theme.base_colors[8];
    const ImVec4& base09 = theme.base_colors[9];
    const ImVec4& base0A = theme.base_colors[10];
    const ImVec4& base0B = theme.base_colors[11];
    const ImVec4& base0C = theme.base_colors[12];
    const ImVec4& base0D = theme.base_colors[13];
    const ImVec4& base0E = theme.base_colors[14];
    const ImVec4& base0F = theme.base_colors[15];

    colors[ImGuiCol_Text] = base05;
    colors[ImGuiCol_TextDisabled] = base03;
    colors[ImGuiCol_WindowBg] = base00;
    colors[ImGuiCol_ChildBg] = base00;
    colors[ImGuiCol_PopupBg] = base00;
    colors[ImGuiCol_Border] = withAlpha(base03, 0.65F);
    colors[ImGuiCol_BorderShadow] = withAlpha(base00, 0.0F);
    colors[ImGuiCol_FrameBg] = base01;
    colors[ImGuiCol_FrameBgHovered] = base02;
    colors[ImGuiCol_FrameBgActive] = base02;
    colors[ImGuiCol_TitleBg] = base01;
    colors[ImGuiCol_TitleBgActive] = base02;
    colors[ImGuiCol_TitleBgCollapsed] = withAlpha(base06, dark_background ? 0.12F : 0.05F);
    colors[ImGuiCol_MenuBarBg] = base01;
    colors[ImGuiCol_ScrollbarBg] = base01;
    colors[ImGuiCol_ScrollbarGrab] = base03;
    colors[ImGuiCol_ScrollbarGrabHovered] = base04;
    colors[ImGuiCol_ScrollbarGrabActive] = base05;
    colors[ImGuiCol_CheckMark] = base0D;
    colors[ImGuiCol_SliderGrab] = base0D;
    colors[ImGuiCol_SliderGrabActive] = base0C;
    colors[ImGuiCol_Button] = base01;
    colors[ImGuiCol_ButtonHovered] = base02;
    colors[ImGuiCol_ButtonActive] = base03;
    colors[ImGuiCol_Header] = withAlpha(base0D, dark_background ? 0.35F : 0.20F);
    colors[ImGuiCol_HeaderHovered] = withAlpha(base0D, dark_background ? 0.50F : 0.30F);
    colors[ImGuiCol_HeaderActive] = withAlpha(base0D, dark_background ? 0.65F : 0.40F);
    colors[ImGuiCol_Separator] = base03;
    colors[ImGuiCol_SeparatorHovered] = base09;
    colors[ImGuiCol_SeparatorActive] = base0E;
    colors[ImGuiCol_ResizeGrip] = withAlpha(base03, 0.5F);
    colors[ImGuiCol_ResizeGripHovered] = base0D;
    colors[ImGuiCol_ResizeGripActive] = base0E;
    colors[ImGuiCol_Tab] = base01;
    colors[ImGuiCol_TabHovered] = base02;
    colors[ImGuiCol_TabActive] = withAlpha(base0D, dark_background ? 0.35F : 0.25F);
    colors[ImGuiCol_TabUnfocused] = base01;
    colors[ImGuiCol_TabUnfocusedActive] = base02;
    colors[ImGuiCol_DockingPreview] = withAlpha(base0D, 0.60F);
    colors[ImGuiCol_DockingEmptyBg] = base00;
    colors[ImGuiCol_PlotLines] = base0D;
    colors[ImGuiCol_PlotLinesHovered] = base0B;
    colors[ImGuiCol_PlotHistogram] = base0A;
    colors[ImGuiCol_PlotHistogramHovered] = base0F;
    colors[ImGuiCol_TableHeaderBg] = base01;
    colors[ImGuiCol_TableBorderStrong] = base03;
    colors[ImGuiCol_TableBorderLight] = withAlpha(base03, 0.45F);
    colors[ImGuiCol_TableRowBg] = withAlpha(base00, 0.0F);
    colors[ImGuiCol_TableRowBgAlt] = withAlpha(base01, 0.45F);
    colors[ImGuiCol_TextSelectedBg] = withAlpha(base0D, dark_background ? 0.35F : 0.20F);
    colors[ImGuiCol_DragDropTarget] = base08;
    colors[ImGuiCol_NavHighlight] = base0D;
    colors[ImGuiCol_NavWindowingHighlight] = withAlpha(base07, 0.70F);
    colors[ImGuiCol_NavWindowingDimBg] = withAlpha(base00, 0.25F);
    colors[ImGuiCol_ModalWindowDimBg] = withAlpha(base00, 0.35F);

    if (luminance(base00) > 0.85F && luminance(base05) < 0.45F) {
        colors[ImGuiCol_DockingEmptyBg] = base01;
    }

    if (clear_color != nullptr) {
        *clear_color = withAlpha(base00, 1.0F);
    }
    applyViewportStyleAdjustments();
}

std::filesystem::path findThemesDirectory() {
    std::filesystem::path current = std::filesystem::current_path();
    for (int depth = 0; depth < 6; ++depth) {
        const std::filesystem::path candidate = current / "themes";
        if (std::filesystem::is_directory(candidate)) {
            return candidate;
        }
        if (current == current.root_path()) {
            break;
        }
        current = current.parent_path();
    }
    return std::filesystem::current_path() / "themes";
}

std::filesystem::path preferencesPath() {
#if defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / "Library" / "Application Support" / "meshtools" / "preferences.ini";
    }
#elif defined(_WIN32)
    if (const char* app_data = std::getenv("APPDATA"); app_data != nullptr && *app_data != '\0') {
        return std::filesystem::path(app_data) / "meshtools" / "preferences.ini";
    }
#else
    if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME"); xdg_config_home != nullptr && *xdg_config_home != '\0') {
        return std::filesystem::path(xdg_config_home) / "meshtools" / "preferences.ini";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "meshtools" / "preferences.ini";
    }
#endif
    return std::filesystem::current_path() / "meshtools-preferences.ini";
}

std::optional<std::string> loadSavedThemeId(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::nullopt;
    }

    std::string line;
    while (std::getline(input, line)) {
        line = trim(std::move(line));
        if (line.empty() || line.front() == '#' || line.front() == ';' || line.front() == '[') {
            continue;
        }
        constexpr std::string_view prefix = "theme=";
        if (std::string_view(line).substr(0, prefix.size()) == prefix) {
            return unquote(line.substr(prefix.size()));
        }
    }

    return std::nullopt;
}

bool writeSavedThemeId(const std::filesystem::path& path, const std::string& theme_id) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }

    output << "[appearance]\n";
    output << "theme=" << theme_id << '\n';
    return output.good();
}

std::optional<ImGuiSystem::ThemeOption> loadThemeFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::nullopt;
    }

    ImGuiSystem::ThemeOption theme;
    theme.id = path.stem().string();

    std::array<bool, 16> found_colors{};
    bool found_scheme = false;
    bool found_author = false;

    std::string line;
    while (std::getline(input, line)) {
        line = trim(std::move(line));
        if (line.empty() || line.front() == '#') {
            continue;
        }

        const std::size_t separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));

        if (key == "scheme") {
            theme.scheme = unquote(value);
            found_scheme = !theme.scheme.empty();
            continue;
        }
        if (key == "author") {
            theme.author = unquote(value);
            found_author = !theme.author.empty();
            continue;
        }

        if (key.size() == 6 && key.rfind("base0", 0) == 0) {
            int color_index = -1;
            const char suffix = key.back();
            if (suffix >= '0' && suffix <= '9') {
                color_index = suffix - '0';
            } else if (suffix >= 'A' && suffix <= 'F') {
                color_index = suffix - 'A' + 10;
            }

            if (color_index >= 0) {
                const std::optional<ImVec4> parsed = parseHexColor(value);
                if (!parsed.has_value()) {
                    return std::nullopt;
                }
                theme.base_colors[static_cast<std::size_t>(color_index)] = *parsed;
                found_colors[static_cast<std::size_t>(color_index)] = true;
            }
        }
    }

    if (!found_scheme) {
        theme.scheme = theme.id;
    }
    if (!found_author) {
        theme.author = "Unknown";
    }
    if (!std::all_of(found_colors.begin(), found_colors.end(), [](bool found) { return found; })) {
        return std::nullopt;
    }

    return theme;
}

int pinnedThemeOrder(std::string_view theme_id) {
    if (theme_id == "imgui") {
        return 0;
    }
    if (theme_id == "one-light") {
        return 1;
    }
    if (theme_id == "onedark") {
        return 2;
    }
    if (theme_id == "solarized-dark") {
        return 3;
    }
    if (theme_id == "solarized-light") {
        return 4;
    }
    return 100;
}

std::vector<ImGuiSystem::ThemeOption> loadThemes() {
    std::vector<ImGuiSystem::ThemeOption> themes;
    themes.push_back(makeBuiltinTheme());

    const std::filesystem::path themes_directory = findThemesDirectory();
    if (std::filesystem::is_directory(themes_directory)) {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(themes_directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".yaml") {
                continue;
            }

            std::optional<ImGuiSystem::ThemeOption> theme = loadThemeFile(entry.path());
            if (!theme.has_value() || theme->id == kDefaultThemeId) {
                continue;
            }
            themes.push_back(std::move(*theme));
        }
    }

    std::sort(themes.begin(), themes.end(), [](const ImGuiSystem::ThemeOption& left, const ImGuiSystem::ThemeOption& right) {
        const int left_order = pinnedThemeOrder(left.id);
        const int right_order = pinnedThemeOrder(right.id);
        if (left_order != right_order) {
            return left_order < right_order;
        }
        return lowercase(left.scheme) < lowercase(right.scheme);
    });

    return themes;
}

const ImGuiSystem::ThemeOption* findThemeById(
    const std::vector<ImGuiSystem::ThemeOption>& themes,
    const std::string& theme_id
) {
    const auto iterator = std::find_if(themes.begin(), themes.end(), [&theme_id](const ImGuiSystem::ThemeOption& theme) {
        return theme.id == theme_id;
    });
    return iterator == themes.end() ? nullptr : &(*iterator);
}

}  // namespace

ImGuiSystem::ImGuiSystem(GLFWwindow* window, const char* glsl_version)
    : themes_(loadThemes()) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.Fonts->AddFontDefault();
    mergeSymbolFont(io);

    const std::optional<std::string> saved_theme_id = loadSavedThemeId(preferencesPath());
    if (saved_theme_id.has_value() && findThemeById(themes_, *saved_theme_id) != nullptr) {
        saved_theme_id_ = *saved_theme_id;
    }
    preview_theme_id_ = saved_theme_id_;
    previewThemeById(preview_theme_id_);
    saved_theme_id_ = preview_theme_id_;

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

const std::vector<ImGuiSystem::ThemeOption>& ImGuiSystem::themes() const {
    return themes_;
}

const std::string& ImGuiSystem::previewThemeId() const {
    return preview_theme_id_;
}

const std::string& ImGuiSystem::savedThemeId() const {
    return saved_theme_id_;
}

bool ImGuiSystem::hasUnsavedThemePreview() const {
    return preview_theme_id_ != saved_theme_id_;
}

const ImVec4& ImGuiSystem::clearColor() const {
    return clear_color_;
}

bool ImGuiSystem::previewThemeById(const std::string& theme_id) {
    const ThemeOption* theme = findThemeById(themes_, theme_id);
    if (theme == nullptr) {
        return false;
    }

    preview_theme_id_ = theme->id;
    if (theme->is_builtin) {
        applyBuiltinTheme(&clear_color_);
    } else {
        applyBase16Theme(*theme, &clear_color_);
    }
    return true;
}

bool ImGuiSystem::savePreviewTheme() {
    if (!previewThemeById(preview_theme_id_)) {
        return false;
    }
    if (!writeSavedThemeId(preferencesPath(), preview_theme_id_)) {
        return false;
    }
    saved_theme_id_ = preview_theme_id_;
    return true;
}

bool ImGuiSystem::revertToSavedTheme() {
    return previewThemeById(saved_theme_id_);
}

void ImGuiSystem::discardUnsavedThemePreview() {
    if (hasUnsavedThemePreview()) {
        revertToSavedTheme();
    }
}

}  // namespace meshtools::ui
