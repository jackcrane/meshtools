#include "meshtools/mesh/MeshOperations/Detail.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace meshtools::mesh::operations::detail {

namespace {

constexpr float kEpsilon = 1.0e-6F;
constexpr float kInfiniteCost = std::numeric_limits<float>::infinity();

std::string formatIndices(const std::vector<std::uint32_t>& indices) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < indices.size(); ++index) {
        if (index > 0) {
            stream << ", ";
        }
        stream << indices[index];
    }
    stream << ']';
    return stream.str();
}

std::string formatVec2(const Vec2& value) {
    std::ostringstream stream;
    stream << '(' << value.x << ", " << value.y << ')';
    return stream.str();
}

std::string formatVec3(const Vec3& value) {
    std::ostringstream stream;
    stream << '(' << value.x << ", " << value.y << ", " << value.z << ')';
    return stream.str();
}

void logTriangulationDebug(const std::string& message) {
    std::cout << "[FACEDBG] " << message << std::endl;
}

std::string formatPointKey(const QuantizedPositionKey& key) {
    std::ostringstream stream;
    stream << '(' << key.x << ", " << key.y << ", " << key.z << ')';
    return stream.str();
}

std::vector<std::uint32_t> orderRepresentativePointsFromTopologyEdges(
    const MeshTopology& topology,
    const EntitySelection& selection
) {
    std::vector<QuantizedEdgeKey> selected_edge_keys;
    selected_edge_keys.reserve(selection.edge_indices.size());
    std::unordered_map<QuantizedPositionKey, std::vector<std::size_t>, QuantizedPositionKeyHash> edges_by_point_key;
    std::unordered_map<QuantizedPositionKey, std::uint32_t, QuantizedPositionKeyHash> representative_point_by_key;
    edges_by_point_key.reserve(selection.edge_indices.size() * 2ULL);
    representative_point_by_key.reserve(selection.edge_indices.size() * 2ULL);

    for (const std::uint32_t edge_index : selection.edge_indices) {
        if (edge_index >= topology.edge_points.size() || edge_index >= topology.unique_edge_keys.size()) {
            continue;
        }

        const MeshTopology::EdgePoints& edge_points = topology.edge_points[edge_index];
        if (edge_points.a >= topology.point_keys.size() || edge_points.b >= topology.point_keys.size()) {
            continue;
        }

        const QuantizedPositionKey key_a = topology.point_keys[edge_points.a];
        const QuantizedPositionKey key_b = topology.point_keys[edge_points.b];
        if (key_a == key_b) {
            continue;
        }

        selected_edge_keys.push_back(topology.unique_edge_keys[edge_index]);
        edges_by_point_key[key_a].push_back(selected_edge_keys.size() - 1U);
        edges_by_point_key[key_b].push_back(selected_edge_keys.size() - 1U);

        const auto [it_a, inserted_a] = representative_point_by_key.emplace(key_a, edge_points.a);
        if (!inserted_a) {
            it_a->second = std::min(it_a->second, edge_points.a);
        }
        const auto [it_b, inserted_b] = representative_point_by_key.emplace(key_b, edge_points.b);
        if (!inserted_b) {
            it_b->second = std::min(it_b->second, edge_points.b);
        }
    }

    if (selected_edge_keys.size() < 2U || representative_point_by_key.size() < 3U) {
        logTriangulationDebug(
            "topology edge ordering rejected: selected_edge_keys=" + std::to_string(selected_edge_keys.size()) +
            " representative_points=" + std::to_string(representative_point_by_key.size())
        );
        return {};
    }

    std::size_t endpoint_count = 0;
    bool have_start_key = false;
    QuantizedPositionKey start_key{};
    for (const auto& [point_key, incident_edges] : edges_by_point_key) {
        if (incident_edges.empty() || incident_edges.size() > 2U) {
            logTriangulationDebug(
                "topology edge ordering rejected key=" + formatPointKey(point_key) +
                " degree=" + std::to_string(incident_edges.size())
            );
            return {};
        }
        if (incident_edges.size() == 1U) {
            ++endpoint_count;
            if (!have_start_key || lessThan(point_key, start_key)) {
                start_key = point_key;
                have_start_key = true;
            }
        }
    }

    if (endpoint_count != 0U && endpoint_count != 2U) {
        logTriangulationDebug(
            "topology edge ordering rejected endpoint_count=" + std::to_string(endpoint_count)
        );
        return {};
    }
    if (!have_start_key) {
        for (const auto& [point_key, incident_edges] : edges_by_point_key) {
            (void)incident_edges;
            if (!have_start_key || lessThan(point_key, start_key)) {
                start_key = point_key;
                have_start_key = true;
            }
        }
    }
    if (!have_start_key) {
        logTriangulationDebug("topology edge ordering rejected: no start key");
        return {};
    }

    std::vector<bool> visited_edges(selected_edge_keys.size(), false);
    std::vector<QuantizedPositionKey> ordered_keys = {start_key};
    QuantizedPositionKey current_key = start_key;
    bool have_previous_key = false;
    QuantizedPositionKey previous_key{};
    std::size_t visited_edge_count = 0;

    while (visited_edge_count < selected_edge_keys.size()) {
        std::size_t next_edge_index = selected_edge_keys.size();
        for (const std::size_t edge_list_index : edges_by_point_key[current_key]) {
            if (visited_edges[edge_list_index]) {
                continue;
            }

            const QuantizedEdgeKey& edge_key = selected_edge_keys[edge_list_index];
            const QuantizedPositionKey next_key = edge_key.a == current_key ? edge_key.b : edge_key.a;
            if (have_previous_key && next_key == previous_key && edges_by_point_key[current_key].size() > 1U) {
                continue;
            }

            next_edge_index = edge_list_index;
            break;
        }

        if (next_edge_index == selected_edge_keys.size()) {
            for (const std::size_t edge_list_index : edges_by_point_key[current_key]) {
                if (!visited_edges[edge_list_index]) {
                    next_edge_index = edge_list_index;
                    break;
                }
            }
        }
        if (next_edge_index == selected_edge_keys.size()) {
            break;
        }

        visited_edges[next_edge_index] = true;
        ++visited_edge_count;
        const QuantizedEdgeKey& edge_key = selected_edge_keys[next_edge_index];
        const QuantizedPositionKey next_key = edge_key.a == current_key ? edge_key.b : edge_key.a;
        previous_key = current_key;
        have_previous_key = true;
        current_key = next_key;
        ordered_keys.push_back(current_key);
    }

    if (visited_edge_count != selected_edge_keys.size()) {
        logTriangulationDebug(
            "topology edge ordering failed to visit all edges: visited=" + std::to_string(visited_edge_count) +
            " total=" + std::to_string(selected_edge_keys.size())
        );
        return {};
    }
    if (!ordered_keys.empty() && ordered_keys.front() == ordered_keys.back()) {
        ordered_keys.pop_back();
    }
    if (ordered_keys.size() != representative_point_by_key.size()) {
        logTriangulationDebug(
            "topology edge ordering rejected ordered_keys=" + std::to_string(ordered_keys.size()) +
            " representative_points=" + std::to_string(representative_point_by_key.size())
        );
        return {};
    }

    std::vector<std::uint32_t> ordered_points;
    ordered_points.reserve(ordered_keys.size());
    for (const QuantizedPositionKey& point_key : ordered_keys) {
        const auto representative_it = representative_point_by_key.find(point_key);
        if (representative_it == representative_point_by_key.end()) {
            logTriangulationDebug("topology edge ordering missing representative for key=" + formatPointKey(point_key));
            return {};
        }
        ordered_points.push_back(representative_it->second);
    }

    logTriangulationDebug(
        "topology edge ordering success ordered_points=" + formatIndices(ordered_points) +
        " topological_point_count=" + std::to_string(representative_point_by_key.size())
    );
    return ordered_points;
}

[[nodiscard]] float triangleAreaCost3D(
    const std::vector<Vec3>& positions,
    const Triangle& triangle,
    const Vec3& preferred_normal
) {
    if (triangle.a >= positions.size() || triangle.b >= positions.size() || triangle.c >= positions.size()) {
        return kInfiniteCost;
    }

    const Vec3 edge_ab = subtract(positions[triangle.b], positions[triangle.a]);
    const Vec3 edge_ac = subtract(positions[triangle.c], positions[triangle.a]);
    const Vec3 cross_value = cross(edge_ab, edge_ac);
    const float doubled_area = length(cross_value);
    if (doubled_area <= kEpsilon) {
        return kInfiniteCost;
    }

    float cost = doubled_area;
    if (length(preferred_normal) > kEpsilon) {
        const Vec3 triangle_normal = normalize(cross_value);
        const float alignment = dot(triangle_normal, preferred_normal);
        cost *= 2.0F - std::clamp(alignment, -1.0F, 1.0F);
    }

    return cost;
}

[[nodiscard]] std::vector<Triangle> triangulateBoundaryLoop3D(
    const std::vector<Vec3>& positions,
    const std::vector<std::uint32_t>& ordered_points,
    const Vec3& preferred_normal
) {
    std::vector<Triangle> triangles;
    const std::size_t point_count = ordered_points.size();
    if (point_count < 3) {
        logTriangulationDebug("triangulateBoundaryLoop3D rejected point_count=" + std::to_string(point_count));
        return triangles;
    }

    std::vector<std::vector<float>> best_cost(point_count, std::vector<float>(point_count, kInfiniteCost));
    std::vector<std::vector<std::size_t>> best_split(point_count, std::vector<std::size_t>(point_count, point_count));
    for (std::size_t index = 0; index + 1U < point_count; ++index) {
        best_cost[index][index + 1U] = 0.0F;
    }

    for (std::size_t gap = 2; gap < point_count; ++gap) {
        for (std::size_t first = 0; first + gap < point_count; ++first) {
            const std::size_t second = first + gap;
            for (std::size_t split = first + 1U; split < second; ++split) {
                if (!std::isfinite(best_cost[first][split]) || !std::isfinite(best_cost[split][second])) {
                    continue;
                }

                const Triangle triangle{
                    .a = ordered_points[first],
                    .b = ordered_points[split],
                    .c = ordered_points[second],
                };
                const float triangle_cost = triangleAreaCost3D(positions, triangle, preferred_normal);
                if (!std::isfinite(triangle_cost)) {
                    continue;
                }

                const float candidate_cost =
                    best_cost[first][split] +
                    best_cost[split][second] +
                    triangle_cost;
                if (candidate_cost < best_cost[first][second]) {
                    best_cost[first][second] = candidate_cost;
                    best_split[first][second] = split;
                }
            }
        }
    }

    if (!std::isfinite(best_cost[0][point_count - 1U])) {
        logTriangulationDebug(
            "triangulateBoundaryLoop3D failed to find triangulation for ordered_points=" + formatIndices(ordered_points)
        );
        return {};
    }

    std::vector<std::tuple<std::size_t, std::size_t>> pending = {{0U, point_count - 1U}};
    triangles.reserve(point_count - 2U);
    while (!pending.empty()) {
        const auto [first, second] = pending.back();
        pending.pop_back();
        if (second <= first + 1U) {
            continue;
        }

        const std::size_t split = best_split[first][second];
        if (split >= point_count) {
            logTriangulationDebug(
                "triangulateBoundaryLoop3D invalid split first=" + std::to_string(first) +
                " second=" + std::to_string(second)
            );
            return {};
        }

        triangles.push_back(Triangle{
            .a = ordered_points[first],
            .b = ordered_points[split],
            .c = ordered_points[second],
        });
        pending.emplace_back(first, split);
        pending.emplace_back(split, second);
    }

    logTriangulationDebug(
        "triangulateBoundaryLoop3D success triangle_count=" + std::to_string(triangles.size()) +
        " ordered_points=" + formatIndices(ordered_points)
    );
    return triangles;
}

}  // namespace

std::vector<std::uint32_t> orderPointsFromEdges(
    const MeshTopology& topology,
    const std::vector<Vec3>& positions,
    const EntitySelection& selection,
    Vec3 preferred_normal
) {
    if (std::vector<std::uint32_t> topology_ordered_points =
            orderRepresentativePointsFromTopologyEdges(topology, selection);
        topology_ordered_points.size() >= 3U) {
        return topology_ordered_points;
    }

    std::vector<MeshTopology::EdgePoints> selected_edges;
    selected_edges.reserve(selection.edge_indices.size());
    for (const std::uint32_t edge_index : selection.edge_indices) {
        if (edge_index < topology.edge_points.size()) {
            selected_edges.push_back(topology.edge_points[edge_index]);
        }
    }
    if (selected_edges.size() < 2) {
        logTriangulationDebug("orderPointsFromEdges rejected selected_edges.size()=" + std::to_string(selected_edges.size()));
        return {};
    }

    std::unordered_map<std::uint32_t, std::vector<std::size_t>> edges_by_point;
    edges_by_point.reserve(selected_edges.size() * 2ULL);
    for (std::size_t index = 0; index < selected_edges.size(); ++index) {
        edges_by_point[selected_edges[index].a].push_back(index);
        edges_by_point[selected_edges[index].b].push_back(index);
    }

    std::uint32_t start_point = std::numeric_limits<std::uint32_t>::max();
    std::size_t endpoint_count = 0;
    for (const auto& [point_index, incident_edges] : edges_by_point) {
        if (incident_edges.empty() || incident_edges.size() > 2U) {
            logTriangulationDebug(
                "orderPointsFromEdges fell back to connected-points ordering at point=" + std::to_string(point_index) +
                " degree=" + std::to_string(incident_edges.size())
            );
            return orderPointsFromConnectedPoints(
                topology,
                positions,
                collectPointsFromSelectedEdges(topology, selection),
                preferred_normal
            );
        }
        if (incident_edges.size() == 1U && point_index < start_point) {
            ++endpoint_count;
            start_point = point_index;
        }
    }
    if (endpoint_count != 0U && endpoint_count != 2U) {
        logTriangulationDebug(
            "orderPointsFromEdges fell back due to endpoint_count=" + std::to_string(endpoint_count)
        );
        return orderPointsFromConnectedPoints(
            topology,
            positions,
            collectPointsFromSelectedEdges(topology, selection),
            preferred_normal
        );
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

    if (visited_edge_count != selected_edges.size()) {
        logTriangulationDebug(
            "orderPointsFromEdges fell back because visited_edge_count=" + std::to_string(visited_edge_count) +
            " selected_edges=" + std::to_string(selected_edges.size())
        );
        return orderPointsFromConnectedPoints(
            topology,
            positions,
            collectPointsFromSelectedEdges(topology, selection),
            preferred_normal
        );
    }
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
    if (unique_ordered_points.size() < 3) {
        logTriangulationDebug(
            "orderPointsFromEdges fell back because unique_ordered_points.size()=" +
            std::to_string(unique_ordered_points.size())
        );
        return orderPointsFromConnectedPoints(
            topology,
            positions,
            collectPointsFromSelectedEdges(topology, selection),
            preferred_normal
        );
    }

    logTriangulationDebug("orderPointsFromEdges success ordered_points=" + formatIndices(unique_ordered_points));
    return unique_ordered_points;
}

std::vector<Triangle> triangulateOrderedFace(
    const std::vector<Vec3>& positions,
    std::vector<std::uint32_t> ordered_points,
    Vec3 preferred_normal
) {
    std::vector<Triangle> triangles;
    if (ordered_points.size() < 3) {
        logTriangulationDebug("triangulateOrderedFace rejected ordered_points.size()=" + std::to_string(ordered_points.size()));
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
        logTriangulationDebug(
            "triangulateOrderedFace rejected because polygon_normal was zero; ordered_points=" +
            formatIndices(ordered_points)
        );
        return triangles;
    }
    logTriangulationDebug(
        "triangulateOrderedFace start ordered_points=" + formatIndices(ordered_points) +
        " preferred_normal=" + formatVec3(preferred_normal) +
        " polygon_normal=" + formatVec3(polygon_normal)
    );

    const Vec3 centroid = computeCentroid(positions, ordered_points);
    Vec3 tangent{};
    Vec3 bitangent{};
    buildPlaneBasis(polygon_normal, &tangent, &bitangent);

    std::vector<Vec2> projected_points;
    projected_points.reserve(ordered_points.size());
    for (const std::uint32_t point_index : ordered_points) {
        projected_points.push_back(projectPointToPlane(positions, point_index, centroid, tangent, bitangent));
    }

    std::vector<std::uint32_t> sanitized_points;
    std::vector<Vec2> sanitized_projected_points;
    sanitized_points.reserve(ordered_points.size());
    sanitized_projected_points.reserve(projected_points.size());
    for (std::size_t index = 0; index < ordered_points.size(); ++index) {
        const std::uint32_t point_index = ordered_points[index];
        if (point_index >= positions.size()) {
            continue;
        }
        if (!sanitized_points.empty() && sanitized_points.back() == point_index) {
            continue;
        }

        sanitized_points.push_back(point_index);
        sanitized_projected_points.push_back(projected_points[index]);
    }
    if (sanitized_points.size() > 1 && sanitized_points.front() == sanitized_points.back()) {
        sanitized_points.pop_back();
        sanitized_projected_points.pop_back();
    }
    ordered_points = std::move(sanitized_points);
    projected_points = std::move(sanitized_projected_points);
    if (ordered_points.size() < 3) {
        logTriangulationDebug("triangulateOrderedFace rejected after sanitizing ordered_points");
        return {};
    }
    logTriangulationDebug(
        "triangulateOrderedFace sanitized ordered_points=" + formatIndices(ordered_points) +
        " first_projected=" + (projected_points.empty() ? std::string("n/a") : formatVec2(projected_points.front()))
    );

    if (signedArea2D(projected_points) < 0.0F) {
        std::reverse(ordered_points.begin(), ordered_points.end());
        std::reverse(projected_points.begin(), projected_points.end());
        logTriangulationDebug("triangulateOrderedFace reversed winding after signedArea2D check");
    }

    const bool untangled = untanglePolygonOrder(&ordered_points, &projected_points);
    logTriangulationDebug(
        "triangulateOrderedFace untanglePolygonOrder=" + std::string(untangled ? "true" : "false") +
        " ordered_points=" + formatIndices(ordered_points)
    );
    triangles = triangulateBoundaryLoop3D(positions, ordered_points, preferred_normal);
    if (triangles.size() != ordered_points.size() - 2U) {
        logTriangulationDebug(
            "triangulateOrderedFace rejected because triangle_count=" + std::to_string(triangles.size()) +
            " expected=" + std::to_string(ordered_points.size() - 2U)
        );
        return {};
    }

    logTriangulationDebug("triangulateOrderedFace success triangle_count=" + std::to_string(triangles.size()));
    return triangles;
}

}  // namespace meshtools::mesh::operations::detail
