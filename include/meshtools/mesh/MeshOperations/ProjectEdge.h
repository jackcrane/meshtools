#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "meshtools/mesh/MeshDocument.h"

namespace meshtools::mesh {

struct ModifyProjectAvailability {
    std::size_t face_count = 0;
    bool available = false;
    std::string unavailable_reason;

    [[nodiscard]] bool any() const {
        return available;
    }
};

struct ModifyProjectResult {
    bool changed = false;
    std::optional<std::uint32_t> created_edge_index;
};

enum class ModifyProjectTargetType {
    Face,
    Edge,
    Point,
};

struct ModifyProjectTarget {
    ModifyProjectTargetType type = ModifyProjectTargetType::Face;
    std::uint32_t index = 0;
};

struct ModifyProjectOptions {
    std::array<std::uint32_t, 2> source_face_indices = {0, 0};
    bool infinite_length = true;
    std::optional<ModifyProjectTarget> start_target;
    std::optional<ModifyProjectTarget> end_target;
};

[[nodiscard]] ModifyProjectAvailability computeModifyProjectAvailability(
    const MeshDocument& document,
    const EntitySelection& selection
);
[[nodiscard]] ModifyProjectResult applyModifyProject(
    MeshDocument* document,
    const ModifyProjectOptions& options
);

}  // namespace meshtools::mesh
