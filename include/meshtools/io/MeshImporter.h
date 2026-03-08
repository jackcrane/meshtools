#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "meshtools/mesh/MeshDocument.h"

namespace meshtools::io {

struct MeshImportResult {
    std::optional<mesh::MeshDocument> document;
    std::string error_message;

    [[nodiscard]] bool succeeded() const {
        return document.has_value();
    }
};

MeshImportResult importMeshFromFile(const std::filesystem::path& path);

}  // namespace meshtools::io

