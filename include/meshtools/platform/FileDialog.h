#pragma once

#include <filesystem>
#include <optional>

namespace meshtools::platform {

std::optional<std::filesystem::path> openMeshFileDialog();

}  // namespace meshtools::platform

