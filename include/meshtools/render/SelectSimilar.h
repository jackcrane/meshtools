#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "meshtools/mesh/MeshDocument.h"

namespace meshtools::render {

struct SimilarityEdge {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
};

struct SelectSimilarParams {
    bool allow_rotation_x = true;
    bool allow_rotation_y = true;
    bool allow_rotation_z = true;
    bool allow_scaling = false;
    bool require_uniform_scaling = true;
    float tolerance = 1.0F;
};

struct SelectSimilarResult {
    bool available = false;
    std::size_t match_count = 0;
    mesh::EntitySelection selection;
    std::vector<std::uint32_t> preview_edge_indices;
    std::vector<std::string> unavailable_reasons;
};

[[nodiscard]] SelectSimilarResult evaluateSelectSimilar(
    const std::vector<mesh::Vec3>& positions,
    std::span<const SimilarityEdge> edges,
    const mesh::EntitySelection& current_selection,
    const SelectSimilarParams& params
);

}  // namespace meshtools::render
