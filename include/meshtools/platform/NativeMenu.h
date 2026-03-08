#pragma once

#include <filesystem>
#include <string>

namespace meshtools::platform {

struct NativeMenuActions {
    bool open_document = false;
    bool save_project = false;
    bool save_project_as = false;
    std::filesystem::path open_sample_path;
    bool open_settings = false;
    bool quit = false;
};

void initializeNativeMenu(const std::string& app_name);
NativeMenuActions consumePendingNativeMenuActions();

}  // namespace meshtools::platform
