#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "meshtools/mesh/MeshDocument.h"

namespace meshtools::io {

using MeshImportProgressCallback = std::function<void(float progress, std::string_view stage)>;

struct MeshImportResult {
    std::optional<mesh::MeshDocument> document;
    std::string error_message;

    [[nodiscard]] bool succeeded() const {
        return document.has_value();
    }
};

MeshImportResult importMeshFromFile(
    const std::filesystem::path& path,
    const MeshImportProgressCallback& progress_callback = {}
);

}  // namespace meshtools::io
