#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "meshtools/mesh/MeshDocument.h"

namespace meshtools::io {

inline constexpr const char* kProjectFormatVersion = "0.0.1";

using ProjectArchiveLoadProgressCallback = std::function<void(float progress, std::string_view stage)>;

struct ProjectArchiveLoadResult {
    std::optional<mesh::MeshDocument> document;
    std::vector<std::string> log_messages;
    std::string error_message;

    [[nodiscard]] bool succeeded() const {
        return document.has_value();
    }
};

struct ProjectArchiveSaveInput {
    const mesh::MeshDocument& document;
    std::span<const std::string> log_messages;
};

struct ProjectArchiveSaveResult {
    std::string error_message;

    [[nodiscard]] bool succeeded() const {
        return error_message.empty();
    }
};

ProjectArchiveLoadResult loadProjectArchive(
    const std::filesystem::path& archive_path,
    const ProjectArchiveLoadProgressCallback& progress_callback = {}
);
ProjectArchiveSaveResult saveProjectArchive(
    const std::filesystem::path& archive_path,
    const ProjectArchiveSaveInput& input
);

}  // namespace meshtools::io
