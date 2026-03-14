#include "meshtools/mesh/MeshOperations/Detail.h"

#include <algorithm>
#include <array>

namespace meshtools::mesh::operations::detail {

std::vector<Vec3> normalizePositionsForTopology(const MeshDocument& document) {
    std::vector<Vec3> normalized_positions;
    normalized_positions.reserve(document.positions.size());

    const Vec3 minimum = document.bounds.minimum;
    const Vec3 maximum = document.bounds.maximum;
    const Vec3 center{
        .x = (minimum.x + maximum.x) * 0.5F,
        .y = (minimum.y + maximum.y) * 0.5F,
        .z = (minimum.z + maximum.z) * 0.5F,
    };
    const float extent_x = maximum.x - minimum.x;
    const float extent_y = maximum.y - minimum.y;
    const float extent_z = maximum.z - minimum.z;
    const float largest_extent = std::max({extent_x, extent_y, extent_z, 0.0001F});
    const float model_scale = 1.8F / largest_extent;

    for (const Vec3& position : document.positions) {
        Vec3 normalized_position{
            .x = (position.x - center.x) * model_scale,
            .y = (position.y - center.y) * model_scale,
            .z = (position.z - center.z) * model_scale,
        };
        if (document.up_axis == UpAxis::Z) {
            normalized_position = rotateXAxisNegative90(normalized_position);
        }
        normalized_positions.push_back(normalized_position);
    }

    return normalized_positions;
}

void sortAndUnique(std::vector<std::uint32_t>& indices) {
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
}

std::vector<std::uint32_t> uniqueSortedIndices(std::vector<std::uint32_t> indices) {
    sortAndUnique(indices);
    return indices;
}

MeshTopology buildMeshTopology(const MeshDocument& document) {
    MeshTopology topology;
    topology.point_keys.reserve(document.positions.size());
    topology.face_indices_by_point.assign(document.positions.size(), {});
    topology.unique_edge_keys.reserve((document.triangles.size() * 3ULL) + document.explicit_edges.size());
    topology.face_indices_by_edge.reserve((document.triangles.size() * 3ULL) + document.explicit_edges.size());
    topology.edge_is_explicit.reserve((document.triangles.size() * 3ULL) + document.explicit_edges.size());
    topology.explicit_edge_document_indices.reserve((document.triangles.size() * 3ULL) + document.explicit_edges.size());
    topology.edge_index_by_key.reserve((document.triangles.size() * 3ULL) + document.explicit_edges.size());

    const std::vector<Vec3> normalized_positions = normalizePositionsForTopology(document);
    for (const Vec3& position : normalized_positions) {
        topology.point_keys.push_back(makeQuantizedPositionKey(position));
    }

    for (std::size_t face_index = 0; face_index < document.triangles.size(); ++face_index) {
        const Triangle& triangle = document.triangles[face_index];
        const std::array<std::uint32_t, 3> point_indices = {triangle.a, triangle.b, triangle.c};

        for (const std::uint32_t point_index : point_indices) {
            if (point_index < topology.face_indices_by_point.size()) {
                topology.face_indices_by_point[point_index].push_back(static_cast<std::uint32_t>(face_index));
            }
        }

        if (triangle.a >= topology.point_keys.size() ||
            triangle.b >= topology.point_keys.size() ||
            triangle.c >= topology.point_keys.size()) {
            continue;
        }

        const std::array<QuantizedEdgeKey, 3> edge_keys = {
            makeQuantizedEdgeKey(topology.point_keys[triangle.a], topology.point_keys[triangle.b]),
            makeQuantizedEdgeKey(topology.point_keys[triangle.b], topology.point_keys[triangle.c]),
            makeQuantizedEdgeKey(topology.point_keys[triangle.c], topology.point_keys[triangle.a]),
        };
        const std::array<MeshTopology::EdgePoints, 3> edge_points = {{
            MeshTopology::EdgePoints{.a = triangle.a, .b = triangle.b},
            MeshTopology::EdgePoints{.a = triangle.b, .b = triangle.c},
            MeshTopology::EdgePoints{.a = triangle.c, .b = triangle.a},
        }};

        for (std::size_t edge_offset = 0; edge_offset < edge_keys.size(); ++edge_offset) {
            const auto [iterator, inserted] = topology.edge_index_by_key.emplace(
                edge_keys[edge_offset],
                static_cast<std::uint32_t>(topology.unique_edge_keys.size())
            );
            if (inserted) {
                topology.unique_edge_keys.push_back(edge_keys[edge_offset]);
                topology.edge_points.push_back(edge_points[edge_offset]);
                topology.edge_is_explicit.push_back(false);
                topology.explicit_edge_document_indices.push_back(kInvalidIndex);
                topology.face_indices_by_edge.emplace_back();
            }

            topology.face_indices_by_edge[iterator->second].push_back(static_cast<std::uint32_t>(face_index));
        }
    }

    for (std::size_t explicit_edge_index = 0; explicit_edge_index < document.explicit_edges.size(); ++explicit_edge_index) {
        const EdgeSegment& edge = document.explicit_edges[explicit_edge_index];
        if (edge.a >= topology.point_keys.size() || edge.b >= topology.point_keys.size()) {
            continue;
        }

        const auto [iterator, inserted] = topology.edge_index_by_key.emplace(
            makeQuantizedEdgeKey(topology.point_keys[edge.a], topology.point_keys[edge.b]),
            static_cast<std::uint32_t>(topology.unique_edge_keys.size())
        );
        if (inserted) {
            topology.unique_edge_keys.push_back(iterator->first);
            topology.edge_points.push_back(MeshTopology::EdgePoints{.a = edge.a, .b = edge.b});
            topology.edge_is_explicit.push_back(true);
            topology.explicit_edge_document_indices.push_back(static_cast<std::uint32_t>(explicit_edge_index));
            topology.face_indices_by_edge.emplace_back();
        }
    }

    return topology;
}

std::vector<std::uint32_t> collectPointsFromSelectedEdges(const MeshTopology& topology, const EntitySelection& selection) {
    std::vector<std::uint32_t> point_indices;
    point_indices.reserve(selection.edge_indices.size() * 2ULL);
    for (const std::uint32_t edge_index : selection.edge_indices) {
        if (edge_index < topology.edge_points.size()) {
            point_indices.push_back(topology.edge_points[edge_index].a);
            point_indices.push_back(topology.edge_points[edge_index].b);
        }
    }

    return uniqueSortedIndices(std::move(point_indices));
}

std::vector<std::uint32_t> selectedFaceIndices(const MeshDocument& document, const EntitySelection& selection) {
    std::vector<std::uint32_t> face_indices;
    face_indices.reserve(selection.face_indices.size());
    for (const std::uint32_t face_index : selection.face_indices) {
        if (face_index < document.triangles.size()) {
            face_indices.push_back(face_index);
        }
    }

    return uniqueSortedIndices(std::move(face_indices));
}

}  // namespace meshtools::mesh::operations::detail
