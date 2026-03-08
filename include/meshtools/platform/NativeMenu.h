#pragma once

#include <string>

namespace meshtools::platform {

struct NativeMenuActions {
    bool open_document = false;
    bool save_project = false;
    bool open_settings = false;
    bool quit = false;
};

void initializeNativeMenu(const std::string& app_name);
NativeMenuActions consumePendingNativeMenuActions();

}  // namespace meshtools::platform
