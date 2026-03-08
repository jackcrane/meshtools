#include "meshtools/platform/FileDialog.h"

namespace meshtools::platform {

std::optional<std::filesystem::path> openDocumentFileDialog() {
    return std::nullopt;
}

std::optional<std::filesystem::path> saveProjectFileDialog() {
    return std::nullopt;
}

}  // namespace meshtools::platform
