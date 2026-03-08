#pragma once

#include <filesystem>
#include <optional>

namespace meshtools::platform {

std::optional<std::filesystem::path> openDocumentFileDialog();
std::optional<std::filesystem::path> saveProjectFileDialog();

}  // namespace meshtools::platform
