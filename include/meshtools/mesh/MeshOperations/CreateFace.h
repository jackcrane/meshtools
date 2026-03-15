#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "meshtools/mesh/MeshDocument.h"

namespace meshtools::mesh {

namespace operations::detail {
struct MeshTopology;
}

struct ModifyCreateFaceAvailability {
    std::size_t point_count = 0;
    std::size_t edge_count = 0;
    std::size_t candidate_point_count = 0;

    [[nodiscard]] bool any() const {
        return point_count >= 3 || (edge_count >= 2 && candidate_point_count >= 3);
    }
};

struct ModifyCreateFaceResult {
    bool changed = false;
    std::size_t created_face_count = 0;
    std::vector<std::uint32_t> created_face_indices;
};

[[nodiscard]] ModifyCreateFaceAvailability computeModifyCreateFaceAvailability(
    const MeshDocument& document,
    const EntitySelection& selection
);
[[nodiscard]] ModifyCreateFaceAvailability computeModifyCreateFaceAvailability(
    const MeshDocument& document,
    const operations::detail::MeshTopology& topology,
    const EntitySelection& selection
);
[[nodiscard]] ModifyCreateFaceResult applyModifyCreateFace(
    MeshDocument* document,
    const EntitySelection& selection
);

}  // namespace meshtools::mesh
