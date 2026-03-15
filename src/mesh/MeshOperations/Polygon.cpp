#include "meshtools/mesh/MeshOperations/Detail.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace meshtools::mesh::operations::detail {

namespace {

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

void logPolygonDebug(const std::string& message) {
    std::cout << "[FACEDBG] " << message << std::endl;
}

struct CandidateEdge {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
};

std::vector<std::uint32_t> walkPointChain(
    const std::vector<CandidateEdge>& edges,
    std::size_t expected_point_count
) {
    if (edges.empty() || expected_point_count < 3) {
        logPolygonDebug(
            "walkPointChain rejected edge set: edges=" + std::to_string(edges.size()) +
            " expected_point_count=" + std::to_string(expected_point_count)
        );
        return {};
    }

    std::unordered_map<std::uint32_t, std::vector<std::size_t>> edges_by_point;
    edges_by_point.reserve(expected_point_count);
    for (std::size_t edge_index = 0; edge_index < edges.size(); ++edge_index) {
        edges_by_point[edges[edge_index].a].push_back(edge_index);
        edges_by_point[edges[edge_index].b].push_back(edge_index);
    }

    std::size_t endpoint_count = 0;
    std::uint32_t start_point = std::numeric_limits<std::uint32_t>::max();
    for (const auto& [point_index, incident_edges] : edges_by_point) {
        if (incident_edges.empty() || incident_edges.size() > 2U) {
            logPolygonDebug(
                "walkPointChain rejected non-manifold point " + std::to_string(point_index) +
                " degree=" + std::to_string(incident_edges.size())
            );
            return {};
        }
        if (incident_edges.size() == 1U) {
            ++endpoint_count;
            start_point = std::min(start_point, point_index);
        }
    }

    const bool is_cycle = endpoint_count == 0U;
    if (!is_cycle && endpoint_count != 2U) {
        logPolygonDebug("walkPointChain rejected graph: endpoint_count=" + std::to_string(endpoint_count));
        return {};
    }
    if (is_cycle) {
        start_point = std::min_element(
            edges.begin(),
            edges.end(),
            [](const CandidateEdge& left, const CandidateEdge& right) {
                return std::tie(left.a, left.b) < std::tie(right.a, right.b);
            }
        )->a;
    }

    std::vector<bool> visited_edges(edges.size(), false);
    std::vector<std::uint32_t> ordered_points = {start_point};
    std::uint32_t current_point = start_point;
    std::uint32_t previous_point = kInvalidIndex;
    std::size_t visited_edge_count = 0;

    while (visited_edge_count < edges.size()) {
        std::size_t next_edge_index = edges.size();
        for (const std::size_t edge_index : edges_by_point[current_point]) {
            if (visited_edges[edge_index]) {
                continue;
            }

            const CandidateEdge& edge = edges[edge_index];
            const std::uint32_t candidate_point = edge.a == current_point ? edge.b : edge.a;
            if (candidate_point == previous_point && edges_by_point[current_point].size() > 1U) {
                continue;
            }

            next_edge_index = edge_index;
            break;
        }

        if (next_edge_index == edges.size()) {
            for (const std::size_t edge_index : edges_by_point[current_point]) {
                if (!visited_edges[edge_index]) {
                    next_edge_index = edge_index;
                    break;
                }
            }
        }
        if (next_edge_index == edges.size()) {
            break;
        }

        visited_edges[next_edge_index] = true;
        ++visited_edge_count;
        const CandidateEdge& edge = edges[next_edge_index];
        const std::uint32_t next_point = edge.a == current_point ? edge.b : edge.a;
        previous_point = current_point;
        current_point = next_point;
        ordered_points.push_back(current_point);
    }

    if (visited_edge_count != edges.size()) {
        logPolygonDebug(
            "walkPointChain failed to visit all edges: visited=" + std::to_string(visited_edge_count) +
            " total=" + std::to_string(edges.size())
        );
        return {};
    }
    if (!ordered_points.empty() && ordered_points.front() == ordered_points.back()) {
        ordered_points.pop_back();
    }

    std::vector<std::uint32_t> unique_points = ordered_points;
    sortAndUnique(unique_points);
    if (unique_points.size() != expected_point_count || ordered_points.size() != expected_point_count) {
        logPolygonDebug(
            "walkPointChain rejected ordered chain: unique_points=" + std::to_string(unique_points.size()) +
            " ordered_points=" + std::to_string(ordered_points.size()) +
            " expected=" + std::to_string(expected_point_count) +
            " ordered=" + formatIndices(ordered_points)
        );
        return {};
    }

    logPolygonDebug("walkPointChain success ordered_points=" + formatIndices(ordered_points));
    return ordered_points;
}

std::vector<std::uint32_t> orderPointsFromCandidateEdges(
    const MeshTopology& topology,
    std::vector<std::uint32_t> point_indices,
    bool boundary_edges_only
) {
    point_indices = uniqueSortedIndices(std::move(point_indices));
    if (point_indices.size() < 3) {
        logPolygonDebug("orderPointsFromCandidateEdges rejected point set: " + formatIndices(point_indices));
        return {};
    }

    std::unordered_set<std::uint32_t> selected_points(point_indices.begin(), point_indices.end());
    std::vector<CandidateEdge> candidate_edges;
    candidate_edges.reserve(topology.edge_points.size());
    for (std::size_t edge_index = 0; edge_index < topology.edge_points.size(); ++edge_index) {
        const MeshTopology::EdgePoints& edge = topology.edge_points[edge_index];
        if (!selected_points.contains(edge.a) || !selected_points.contains(edge.b)) {
            continue;
        }
        if (boundary_edges_only &&
            !topology.edge_is_explicit[edge_index] &&
            topology.face_indices_by_edge[edge_index].size() > 1U) {
            continue;
        }

        candidate_edges.push_back(CandidateEdge{.a = edge.a, .b = edge.b});
    }

    logPolygonDebug(
        "orderPointsFromCandidateEdges boundary_only=" + std::string(boundary_edges_only ? "true" : "false") +
        " point_indices=" + formatIndices(point_indices) +
        " candidate_edges=" + std::to_string(candidate_edges.size())
    );
    return walkPointChain(candidate_edges, point_indices.size());
}

}  // namespace

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

std::vector<std::uint32_t> orderPointsFromConnectedPoints(
    const MeshTopology& topology,
    const std::vector<Vec3>& positions,
    std::vector<std::uint32_t> point_indices,
    Vec3 preferred_normal
) {
    if (std::vector<std::uint32_t> ordered_indices =
            orderPointsFromCandidateEdges(topology, point_indices, true);
        ordered_indices.size() >= 3) {
        logPolygonDebug("orderPointsFromConnectedPoints used boundary-edge walk");
        return ordered_indices;
    }

    if (std::vector<std::uint32_t> ordered_indices =
            orderPointsFromCandidateEdges(topology, point_indices, false);
        ordered_indices.size() >= 3) {
        logPolygonDebug("orderPointsFromConnectedPoints used all-edge walk");
        return ordered_indices;
    }

    logPolygonDebug("orderPointsFromConnectedPoints falling back to centroid sort");
    return sortPointsForFace(positions, std::move(point_indices), preferred_normal);
}

}  // namespace meshtools::mesh::operations::detail
