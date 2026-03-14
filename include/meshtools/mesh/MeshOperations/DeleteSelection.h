#pragma once

#include <cstddef>

#include "meshtools/mesh/MeshDocument.h"

namespace meshtools::mesh {

struct ModifyDeleteAvailability {
    std::size_t face_count = 0;
    std::size_t inside_edge_count = 0;
    std::size_t outside_edge_count = 0;
    std::size_t point_count = 0;

    [[nodiscard]] bool any() const {
        return face_count > 0 || inside_edge_count > 0 || outside_edge_count > 0 || point_count > 0;
    }
};

struct ModifyDeleteOptions {
    bool faces = false;
    bool inside_edges = false;
    bool outside_edges = false;
    bool points = false;

    [[nodiscard]] bool any() const {
        return faces || inside_edges || outside_edges || points;
    }
};

struct ModifyDeleteResult {
    bool changed = false;
    std::size_t deleted_face_count = 0;
    std::size_t deleted_point_count = 0;
    ModifyDeleteAvailability deleted_selection;
};

[[nodiscard]] ModifyDeleteAvailability computeModifyDeleteAvailability(
    const MeshDocument& document,
    const EntitySelection& selection
);
[[nodiscard]] ModifyDeleteResult applyModifyDelete(
    MeshDocument* document,
    const EntitySelection& selection,
    const ModifyDeleteOptions& options
);

}  // namespace meshtools::mesh
