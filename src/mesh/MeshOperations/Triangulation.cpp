#include "meshtools/mesh/MeshOperations/Detail.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <tuple>
#include <unordered_map>

namespace meshtools::mesh::operations::detail {

std::vector<std::uint32_t> orderPointsFromEdges(
    const MeshTopology& topology,
    const std::vector<Vec3>& positions,
    const EntitySelection& selection,
    Vec3 preferred_normal
) {
    std::vector<MeshTopology::EdgePoints> selected_edges;
    selected_edges.reserve(selection.edge_indices.size());
    for (const std::uint32_t edge_index : selection.edge_indices) {
        if (edge_index < topology.edge_points.size()) {
            selected_edges.push_back(topology.edge_points[edge_index]);
        }
    }
    if (selected_edges.size() < 2) {
        return {};
    }

    std::unordered_map<std::uint32_t, std::vector<std::size_t>> edges_by_point;
    edges_by_point.reserve(selected_edges.size() * 2ULL);
    for (std::size_t index = 0; index < selected_edges.size(); ++index) {
        edges_by_point[selected_edges[index].a].push_back(index);
        edges_by_point[selected_edges[index].b].push_back(index);
    }

    std::uint32_t start_point = std::numeric_limits<std::uint32_t>::max();
    for (const auto& [point_index, incident_edges] : edges_by_point) {
        if (incident_edges.size() == 1U && point_index < start_point) {
            start_point = point_index;
        }
    }
    if (start_point == std::numeric_limits<std::uint32_t>::max()) {
        start_point = std::min_element(
            selected_edges.begin(),
            selected_edges.end(),
            [](const MeshTopology::EdgePoints& left, const MeshTopology::EdgePoints& right) {
                return std::tie(left.a, left.b) < std::tie(right.a, right.b);
            }
        )->a;
    }

    std::vector<bool> visited_edges(selected_edges.size(), false);
    std::vector<std::uint32_t> ordered_points = {start_point};
    std::uint32_t current_point = start_point;
    std::size_t visited_edge_count = 0;

    while (visited_edge_count < selected_edges.size()) {
        bool advanced = false;
        for (const std::size_t edge_list_index : edges_by_point[current_point]) {
            if (visited_edges[edge_list_index]) {
                continue;
            }

            visited_edges[edge_list_index] = true;
            ++visited_edge_count;
            const MeshTopology::EdgePoints& edge = selected_edges[edge_list_index];
            current_point = edge.a == current_point ? edge.b : edge.a;
            ordered_points.push_back(current_point);
            advanced = true;
            break;
        }

        if (!advanced) {
            break;
        }
    }

    if (visited_edge_count == selected_edges.size()) {
        if (ordered_points.size() > 1 && ordered_points.front() == ordered_points.back()) {
            ordered_points.pop_back();
        }

        std::vector<std::uint32_t> unique_ordered_points;
        unique_ordered_points.reserve(ordered_points.size());
        for (const std::uint32_t point_index : ordered_points) {
            if (std::find(unique_ordered_points.begin(), unique_ordered_points.end(), point_index) ==
                unique_ordered_points.end()) {
                unique_ordered_points.push_back(point_index);
            }
        }
        if (unique_ordered_points.size() >= 3) {
            return unique_ordered_points;
        }
    }

    return sortPointsForFace(positions, collectPointsFromSelectedEdges(topology, selection), preferred_normal);
}

std::vector<Triangle> triangulateOrderedFace(
    const std::vector<Vec3>& positions,
    std::vector<std::uint32_t> ordered_points,
    Vec3 preferred_normal
) {
    std::vector<Triangle> triangles;
    if (ordered_points.size() < 3) {
        return triangles;
    }
    Vec3 polygon_normal = preferred_normal;
    if (length(polygon_normal) <= 1.0e-6F) {
        polygon_normal = computePolygonNormal(positions, ordered_points);
    }
    if (length(polygon_normal) <= 1.0e-6F) {
        polygon_normal = findPlaneNormal(positions, ordered_points);
    }
    if (length(polygon_normal) <= 1.0e-6F) {
        return triangles;
    }

    const Vec3 centroid = computeCentroid(positions, ordered_points);
    Vec3 tangent{};
    Vec3 bitangent{};
    buildPlaneBasis(polygon_normal, &tangent, &bitangent);

    std::vector<Vec2> projected_points;
    projected_points.reserve(ordered_points.size());
    for (const std::uint32_t point_index : ordered_points) {
        projected_points.push_back(projectPointToPlane(positions, point_index, centroid, tangent, bitangent));
    }
    if (signedArea2D(projected_points) < 0.0F) {
        std::reverse(ordered_points.begin(), ordered_points.end());
        std::reverse(projected_points.begin(), projected_points.end());
    }

    std::vector<std::size_t> polygon_indices(ordered_points.size());
    std::iota(polygon_indices.begin(), polygon_indices.end(), 0U);
    triangles.reserve(ordered_points.size() - 2U);

    while (polygon_indices.size() >= 3U) {
        bool clipped_ear = false;
        for (std::size_t polygon_index = 0; polygon_index < polygon_indices.size(); ++polygon_index) {
            const std::size_t prev_index =
                polygon_indices[(polygon_index + polygon_indices.size() - 1U) % polygon_indices.size()];
            const std::size_t current_index = polygon_indices[polygon_index];
            const std::size_t next_index = polygon_indices[(polygon_index + 1U) % polygon_indices.size()];

            const Vec2& a = projected_points[prev_index];
            const Vec2& b = projected_points[current_index];
            const Vec2& c = projected_points[next_index];
            if (orient2D(a, b, c) <= 1.0e-6F) {
                continue;
            }

            bool contains_other_point = false;
            for (const std::size_t test_index : polygon_indices) {
                if (test_index == prev_index || test_index == current_index || test_index == next_index) {
                    continue;
                }
                if (pointInTriangle2D(projected_points[test_index], a, b, c)) {
                    contains_other_point = true;
                    break;
                }
            }
            if (contains_other_point) {
                continue;
            }

            const Triangle triangle{
                .a = ordered_points[prev_index],
                .b = ordered_points[current_index],
                .c = ordered_points[next_index],
            };
            if (length(computeTriangleNormal(positions, triangle)) > 1.0e-6F) {
                triangles.push_back(triangle);
            }

            polygon_indices.erase(polygon_indices.begin() + static_cast<std::ptrdiff_t>(polygon_index));
            clipped_ear = true;
            break;
        }

        if (!clipped_ear) {
            triangles.clear();
            for (std::size_t index = 1; index + 1 < ordered_points.size(); ++index) {
                const Triangle triangle{
                    .a = ordered_points[0],
                    .b = ordered_points[index],
                    .c = ordered_points[index + 1],
                };
                if (length(computeTriangleNormal(positions, triangle)) > 1.0e-6F) {
                    triangles.push_back(triangle);
                }
            }
            return triangles;
        }
    }

    return triangles;
}

}  // namespace meshtools::mesh::operations::detail
