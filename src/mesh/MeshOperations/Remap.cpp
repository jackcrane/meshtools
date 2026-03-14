#include "meshtools/mesh/MeshOperations/Detail.h"

namespace meshtools::mesh::operations::detail {

void remapIndexVector(std::vector<std::uint32_t>* indices, const std::vector<std::uint32_t>& old_to_new) {
    if (indices == nullptr) {
        return;
    }

    std::vector<std::uint32_t> remapped;
    remapped.reserve(indices->size());
    for (const std::uint32_t old_index : *indices) {
        if (old_index >= old_to_new.size()) {
            continue;
        }

        const std::uint32_t new_index = old_to_new[old_index];
        if (new_index != kInvalidIndex) {
            remapped.push_back(new_index);
        }
    }

    sortAndUnique(remapped);
    *indices = std::move(remapped);
}

void remapExplicitEdges(std::vector<EdgeSegment>* edges, const std::vector<std::uint32_t>& old_to_new_point) {
    if (edges == nullptr) {
        return;
    }

    std::vector<EdgeSegment> remapped;
    remapped.reserve(edges->size());
    for (const EdgeSegment& edge : *edges) {
        if (edge.a >= old_to_new_point.size() || edge.b >= old_to_new_point.size()) {
            continue;
        }

        const std::uint32_t new_a = old_to_new_point[edge.a];
        const std::uint32_t new_b = old_to_new_point[edge.b];
        if (new_a != kInvalidIndex && new_b != kInvalidIndex && new_a != new_b) {
            remapped.push_back(EdgeSegment{.a = new_a, .b = new_b});
        }
    }

    *edges = std::move(remapped);
}

void remapEdgeIndexVector(
    std::vector<std::uint32_t>* indices,
    const MeshTopology& old_topology,
    const MeshTopology& new_topology
) {
    if (indices == nullptr) {
        return;
    }

    std::vector<std::uint32_t> remapped;
    remapped.reserve(indices->size());
    for (const std::uint32_t old_index : *indices) {
        if (old_index >= old_topology.unique_edge_keys.size()) {
            continue;
        }

        const auto iterator = new_topology.edge_index_by_key.find(old_topology.unique_edge_keys[old_index]);
        if (iterator != new_topology.edge_index_by_key.end()) {
            remapped.push_back(iterator->second);
        }
    }

    sortAndUnique(remapped);
    *indices = std::move(remapped);
}

}  // namespace meshtools::mesh::operations::detail
