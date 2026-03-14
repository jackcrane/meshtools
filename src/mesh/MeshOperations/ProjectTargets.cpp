#include "meshtools/mesh/MeshOperations/Detail.h"

namespace meshtools::mesh::operations::detail {

std::optional<float> resolveProjectTargetParameter(
    const MeshDocument& document,
    const MeshTopology& topology,
    const ModifyProjectTarget& target,
    const Vec3& line_point,
    const Vec3& line_direction,
    float tolerance
) {
    switch (target.type) {
        case ModifyProjectTargetType::Face: {
            const Plane plane = planeFromFace(document, target.index);
            float parameter = 0.0F;
            return linePlaneIntersection(line_point, line_direction, plane, &parameter)
                ? std::optional<float>{parameter}
                : std::nullopt;
        }
        case ModifyProjectTargetType::Edge: {
            if (target.index >= topology.edge_points.size()) {
                return std::nullopt;
            }
            const MeshTopology::EdgePoints edge = topology.edge_points[target.index];
            if (edge.a >= document.positions.size() || edge.b >= document.positions.size()) {
                return std::nullopt;
            }

            const Vec3 edge_origin = document.positions[edge.a];
            const Vec3 edge_direction = subtract(document.positions[edge.b], edge_origin);
            if (length(edge_direction) <= 1.0e-6F) {
                return std::nullopt;
            }

            const ClosestLinePoints closest =
                closestPointsBetweenLines(line_point, line_direction, edge_origin, edge_direction);
            if (!closest.valid || closest.distance > tolerance) {
                return std::nullopt;
            }
            return closest.first_parameter;
        }
        case ModifyProjectTargetType::Point: {
            if (target.index >= document.positions.size()) {
                return std::nullopt;
            }

            const Vec3 point = document.positions[target.index];
            const float parameter = dot(subtract(point, line_point), line_direction);
            const Vec3 projected = add(line_point, scale(line_direction, parameter));
            return length(subtract(projected, point)) <= tolerance
                ? std::optional<float>{parameter}
                : std::nullopt;
        }
    }

    return std::nullopt;
}

std::optional<std::uint32_t> findTopologyEdgeIndexForPoints(
    const MeshTopology& topology,
    const std::vector<Vec3>& normalized_positions,
    std::uint32_t first_point_index,
    std::uint32_t second_point_index
) {
    if (first_point_index >= normalized_positions.size() || second_point_index >= normalized_positions.size()) {
        return std::nullopt;
    }

    const QuantizedEdgeKey edge_key = makeQuantizedEdgeKey(
        makeQuantizedPositionKey(normalized_positions[first_point_index]),
        makeQuantizedPositionKey(normalized_positions[second_point_index])
    );
    const auto iterator = topology.edge_index_by_key.find(edge_key);
    return iterator != topology.edge_index_by_key.end() ? std::optional<std::uint32_t>{iterator->second} : std::nullopt;
}

std::uint32_t findOrAppendPointIndex(MeshDocument* document, const Vec3& position) {
    if (document == nullptr) {
        return kInvalidIndex;
    }

    for (std::uint32_t index = 0; index < document->positions.size(); ++index) {
        if (samePosition(document->positions[index], position)) {
            return index;
        }
    }

    document->positions.push_back(position);
    return static_cast<std::uint32_t>(document->positions.size() - 1U);
}

}  // namespace meshtools::mesh::operations::detail
