#include "meshtools/mesh/MeshOperations/Detail.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace meshtools::mesh::operations::detail {

Vec3 computePolygonNormal(const std::vector<Vec3>& positions, const std::vector<std::uint32_t>& ordered_points) {
    if (ordered_points.size() < 3) {
        return Vec3{};
    }

    Vec3 normal{};
    for (std::size_t index = 0; index < ordered_points.size(); ++index) {
        const std::uint32_t current_index = ordered_points[index];
        const std::uint32_t next_index = ordered_points[(index + 1U) % ordered_points.size()];
        if (current_index >= positions.size() || next_index >= positions.size()) {
            continue;
        }

        const Vec3& current = positions[current_index];
        const Vec3& next = positions[next_index];
        normal.x += (current.y - next.y) * (current.z + next.z);
        normal.y += (current.z - next.z) * (current.x + next.x);
        normal.z += (current.x - next.x) * (current.y + next.y);
    }

    return normalize(normal);
}

Vec3 computeReferenceNormal(
    const MeshDocument& document,
    const MeshTopology& topology,
    const EntitySelection& selection,
    bool from_points
) {
    std::vector<std::uint32_t> face_indices;
    if (from_points) {
        for (const std::uint32_t point_index : selection.point_indices) {
            if (point_index < topology.face_indices_by_point.size()) {
                face_indices.insert(
                    face_indices.end(),
                    topology.face_indices_by_point[point_index].begin(),
                    topology.face_indices_by_point[point_index].end()
                );
            }
        }
    } else {
        for (const std::uint32_t edge_index : selection.edge_indices) {
            if (edge_index < topology.face_indices_by_edge.size()) {
                face_indices.insert(
                    face_indices.end(),
                    topology.face_indices_by_edge[edge_index].begin(),
                    topology.face_indices_by_edge[edge_index].end()
                );
            }
        }
    }

    sortAndUnique(face_indices);
    Vec3 accumulated_normal{};
    for (const std::uint32_t face_index : face_indices) {
        if (face_index < document.triangles.size()) {
            accumulated_normal = add(
                accumulated_normal,
                computeTriangleNormal(document.positions, document.triangles[face_index])
            );
        }
    }
    return normalize(accumulated_normal);
}

Vec3 computeCentroid(const std::vector<Vec3>& positions, const std::vector<std::uint32_t>& point_indices) {
    Vec3 centroid{};
    std::size_t valid_count = 0;
    for (const std::uint32_t point_index : point_indices) {
        if (point_index < positions.size()) {
            centroid = add(centroid, positions[point_index]);
            ++valid_count;
        }
    }

    if (valid_count > 0) {
        centroid.x /= static_cast<float>(valid_count);
        centroid.y /= static_cast<float>(valid_count);
        centroid.z /= static_cast<float>(valid_count);
    }
    return centroid;
}

Vec3 findPlaneNormal(const std::vector<Vec3>& positions, const std::vector<std::uint32_t>& point_indices) {
    for (std::size_t i = 0; i < point_indices.size(); ++i) {
        if (point_indices[i] >= positions.size()) {
            continue;
        }
        for (std::size_t j = i + 1; j < point_indices.size(); ++j) {
            if (point_indices[j] >= positions.size()) {
                continue;
            }
            const Vec3 first = subtract(positions[point_indices[j]], positions[point_indices[i]]);
            for (std::size_t k = j + 1; k < point_indices.size(); ++k) {
                if (point_indices[k] >= positions.size()) {
                    continue;
                }
                const Vec3 second = subtract(positions[point_indices[k]], positions[point_indices[i]]);
                const Vec3 normal = cross(first, second);
                if (length(normal) > 1.0e-6F) {
                    return normalize(normal);
                }
            }
        }
    }

    return Vec3{};
}

std::vector<std::uint32_t> sortPointsForFace(
    const std::vector<Vec3>& positions,
    std::vector<std::uint32_t> point_indices,
    Vec3 preferred_normal
) {
    point_indices = uniqueSortedIndices(std::move(point_indices));
    if (point_indices.size() < 3) {
        return {};
    }

    Vec3 normal = length(preferred_normal) > 1.0e-6F ? preferred_normal : findPlaneNormal(positions, point_indices);
    if (length(normal) <= 1.0e-6F) {
        return {};
    }

    const Vec3 centroid = computeCentroid(positions, point_indices);
    Vec3 tangent{};
    Vec3 bitangent{};
    buildPlaneBasis(normal, &tangent, &bitangent);

    std::vector<std::tuple<float, float, std::uint32_t>> ordered_points;
    ordered_points.reserve(point_indices.size());
    for (const std::uint32_t point_index : point_indices) {
        const Vec2 projected = projectPointToPlane(positions, point_index, centroid, tangent, bitangent);
        ordered_points.emplace_back(
            std::atan2(projected.y, projected.x),
            (projected.x * projected.x) + (projected.y * projected.y),
            point_index
        );
    }

    std::sort(ordered_points.begin(), ordered_points.end(), [](const auto& left, const auto& right) {
        if (std::get<0>(left) != std::get<0>(right)) {
            return std::get<0>(left) < std::get<0>(right);
        }
        if (std::get<1>(left) != std::get<1>(right)) {
            return std::get<1>(left) < std::get<1>(right);
        }
        return std::get<2>(left) < std::get<2>(right);
    });

    std::vector<std::uint32_t> ordered_indices;
    ordered_indices.reserve(ordered_points.size());
    for (const auto& [angle, radius, point_index] : ordered_points) {
        (void)angle;
        (void)radius;
        ordered_indices.push_back(point_index);
    }
    return ordered_indices;
}

}  // namespace meshtools::mesh::operations::detail
