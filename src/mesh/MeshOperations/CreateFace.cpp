#include "meshtools/mesh/MeshOperations/CreateFace.h"
#include "meshtools/mesh/MeshOperations/Detail.h"
#include "meshtools/mesh/MeshOperations/Normals.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <queue>
#include <sstream>
#include <unordered_map>

namespace meshtools::mesh {

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

std::string formatVec3(const Vec3& value) {
    std::ostringstream stream;
    stream << '(' << value.x << ", " << value.y << ", " << value.z << ')';
    return stream.str();
}

void logCreateFaceDebug(const std::string& message) {
    std::cout << "[FACEDBG] " << message << std::endl;
}

constexpr float kEpsilon = 1.0e-6F;
constexpr std::size_t kInvalidLoopIndex = std::numeric_limits<std::size_t>::max();

struct OrderedBoundary {
    std::vector<std::uint32_t> ordered_points;
    Vec3 triangulation_normal;
};

struct ProjectedLoop {
    std::vector<std::uint32_t> ordered_points;
    std::vector<operations::detail::Vec2> projected_points;
    float signed_area = 0.0F;
    float absolute_area = 0.0F;
    std::size_t parent_index = kInvalidLoopIndex;
    std::size_t depth = 0;
};

struct PolygonVertex {
    std::uint32_t point_index = 0;
    operations::detail::Vec2 projected_point;
};

struct BridgeCandidate {
    std::size_t outer_vertex_index = kInvalidLoopIndex;
    std::size_t hole_vertex_index = kInvalidLoopIndex;
    float distance_squared = std::numeric_limits<float>::infinity();

    [[nodiscard]] bool valid() const {
        return outer_vertex_index != kInvalidLoopIndex && hole_vertex_index != kInvalidLoopIndex;
    }
};

[[nodiscard]] bool sameVec2(const operations::detail::Vec2& first, const operations::detail::Vec2& second) {
    return std::abs(first.x - second.x) <= kEpsilon && std::abs(first.y - second.y) <= kEpsilon;
}

[[nodiscard]] float squaredDistance2D(
    const operations::detail::Vec2& first,
    const operations::detail::Vec2& second
) {
    const float delta_x = first.x - second.x;
    const float delta_y = first.y - second.y;
    return (delta_x * delta_x) + (delta_y * delta_y);
}

[[nodiscard]] bool pointOnSegment2D(
    const operations::detail::Vec2& point,
    const operations::detail::Vec2& start,
    const operations::detail::Vec2& end
) {
    if (std::abs(operations::detail::orient2D(start, end, point)) > kEpsilon) {
        return false;
    }

    const float min_x = std::min(start.x, end.x) - kEpsilon;
    const float max_x = std::max(start.x, end.x) + kEpsilon;
    const float min_y = std::min(start.y, end.y) - kEpsilon;
    const float max_y = std::max(start.y, end.y) + kEpsilon;
    return point.x >= min_x && point.x <= max_x && point.y >= min_y && point.y <= max_y;
}

[[nodiscard]] bool pointInPolygon2D(
    const std::vector<operations::detail::Vec2>& polygon,
    const operations::detail::Vec2& point
) {
    if (polygon.size() < 3) {
        return false;
    }

    bool inside = false;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const operations::detail::Vec2& current = polygon[index];
        const operations::detail::Vec2& next = polygon[(index + 1U) % polygon.size()];
        if (pointOnSegment2D(point, current, next)) {
            return true;
        }

        const bool crosses_y = (current.y > point.y) != (next.y > point.y);
        if (!crosses_y) {
            continue;
        }

        const float x_intersection =
            current.x + ((point.y - current.y) * (next.x - current.x) / (next.y - current.y));
        if (x_intersection >= point.x - kEpsilon) {
            inside = !inside;
        }
    }

    return inside;
}

std::vector<std::uint32_t> validSelectedEdgeIndices(
    const operations::detail::MeshTopology& topology,
    const EntitySelection& selection
) {
    std::vector<std::uint32_t> edge_indices;
    edge_indices.reserve(selection.edge_indices.size());
    for (const std::uint32_t edge_index : selection.edge_indices) {
        if (edge_index < topology.edge_points.size()) {
            edge_indices.push_back(edge_index);
        }
    }

    operations::detail::sortAndUnique(edge_indices);
    return edge_indices;
}

std::vector<EntitySelection> splitSelectedEdgeComponents(
    const operations::detail::MeshTopology& topology,
    const EntitySelection& selection
) {
    const std::vector<std::uint32_t> edge_indices = validSelectedEdgeIndices(topology, selection);
    if (edge_indices.empty()) {
        return {};
    }

    std::unordered_map<
        operations::detail::QuantizedPositionKey,
        std::vector<std::size_t>,
        operations::detail::QuantizedPositionKeyHash> edges_by_point;
    edges_by_point.reserve(edge_indices.size() * 2ULL);
    for (std::size_t local_edge_index = 0; local_edge_index < edge_indices.size(); ++local_edge_index) {
        const std::uint32_t edge_index = edge_indices[local_edge_index];
        if (edge_index >= topology.unique_edge_keys.size()) {
            continue;
        }

        const operations::detail::QuantizedEdgeKey& edge_key = topology.unique_edge_keys[edge_index];
        edges_by_point[edge_key.a].push_back(local_edge_index);
        edges_by_point[edge_key.b].push_back(local_edge_index);
    }

    std::vector<EntitySelection> components;
    std::vector<bool> visited(edge_indices.size(), false);
    for (std::size_t local_edge_index = 0; local_edge_index < edge_indices.size(); ++local_edge_index) {
        if (visited[local_edge_index]) {
            continue;
        }

        EntitySelection component;
        std::queue<std::size_t> pending;
        pending.push(local_edge_index);
        visited[local_edge_index] = true;

        while (!pending.empty()) {
            const std::size_t current_edge_index = pending.front();
            pending.pop();

            const std::uint32_t edge_index = edge_indices[current_edge_index];
            component.edge_indices.push_back(edge_index);

            if (edge_index >= topology.unique_edge_keys.size()) {
                continue;
            }

            const operations::detail::QuantizedEdgeKey& edge_key = topology.unique_edge_keys[edge_index];
            const operations::detail::QuantizedPositionKey incident_points[2] = {edge_key.a, edge_key.b};
            for (const operations::detail::QuantizedPositionKey& point_key : incident_points) {
                const auto incident_edges_it = edges_by_point.find(point_key);
                if (incident_edges_it == edges_by_point.end()) {
                    continue;
                }

                for (const std::size_t neighbor_edge_index : incident_edges_it->second) {
                    if (visited[neighbor_edge_index]) {
                        continue;
                    }

                    visited[neighbor_edge_index] = true;
                    pending.push(neighbor_edge_index);
                }
            }
        }

        components.push_back(std::move(component));
    }

    return components;
}

std::optional<OrderedBoundary> orderBoundaryForSelection(
    const MeshDocument& document,
    const operations::detail::MeshTopology& topology,
    const EntitySelection& selection
) {
    const ModifyCreateFaceAvailability availability = computeModifyCreateFaceAvailability(document, topology, selection);
    const Vec3 point_reference_normal =
        operations::detail::computeReferenceNormal(document, topology, selection, true);
    const Vec3 edge_reference_normal =
        operations::detail::computeReferenceNormal(document, topology, selection, false);
    logCreateFaceDebug(
        "component selection point_indices=" + formatIndices(selection.point_indices) +
        " edge_indices=" + formatIndices(selection.edge_indices) +
        " availability(point_count=" + std::to_string(availability.point_count) +
        ", edge_count=" + std::to_string(availability.edge_count) +
        ", candidate_point_count=" + std::to_string(availability.candidate_point_count) + ")"
    );
    logCreateFaceDebug(
        "component reference_normals point=" + formatVec3(point_reference_normal) +
        " edge=" + formatVec3(edge_reference_normal)
    );

    OrderedBoundary boundary;
    std::vector<std::uint32_t> ordered_points;
    bool used_point_selection = false;
    if (availability.point_count >= 3) {
        ordered_points = operations::detail::orderPointsFromConnectedPoints(
            topology,
            document.positions,
            selection.point_indices,
            point_reference_normal
        );
        logCreateFaceDebug("point ordering result count=" + std::to_string(ordered_points.size()) +
                           " ordered_points=" + formatIndices(ordered_points));
        used_point_selection = ordered_points.size() >= 3;
    }
    if (ordered_points.size() < 3 && availability.edge_count >= 2) {
        ordered_points =
            operations::detail::orderPointsFromEdges(topology, document.positions, selection, edge_reference_normal);
        logCreateFaceDebug("edge ordering result count=" + std::to_string(ordered_points.size()) +
                           " ordered_points=" + formatIndices(ordered_points));
        used_point_selection = false;
    }

    if (ordered_points.size() >= 3) {
        const Vec3 polygon_normal = operations::detail::computePolygonNormal(document.positions, ordered_points);
        logCreateFaceDebug(
            "polygon_normal=" + formatVec3(polygon_normal) +
            " triangulation_source=" + std::string(used_point_selection ? "points" : "edges")
        );
        const Vec3 reference_normal = used_point_selection ? point_reference_normal : edge_reference_normal;
        if (operations::detail::length(polygon_normal) > 1.0e-6F &&
            operations::detail::length(reference_normal) > 1.0e-6F &&
            operations::detail::dot(polygon_normal, reference_normal) < 0.0F) {
            std::reverse(ordered_points.begin(), ordered_points.end());
            logCreateFaceDebug("reversed ordered_points to match reference normal; ordered_points=" +
                               formatIndices(ordered_points));
        }
    }

    if (ordered_points.size() < 3) {
        return std::nullopt;
    }

    boundary.ordered_points = std::move(ordered_points);
    boundary.triangulation_normal = used_point_selection ? point_reference_normal : edge_reference_normal;
    return boundary;
}

std::vector<Triangle> createFaceTrianglesForSelection(
    const MeshDocument& document,
    const operations::detail::MeshTopology& topology,
    const EntitySelection& selection
) {
    const std::optional<OrderedBoundary> boundary = orderBoundaryForSelection(document, topology, selection);
    if (!boundary.has_value()) {
        return {};
    }

    return operations::detail::triangulateOrderedFace(
        document.positions,
        boundary->ordered_points,
        boundary->triangulation_normal
    );
}

std::vector<operations::detail::Vec2> projectOrderedPoints(
    const std::vector<Vec3>& positions,
    const std::vector<std::uint32_t>& ordered_points,
    const Vec3& centroid,
    const Vec3& tangent,
    const Vec3& bitangent
) {
    std::vector<operations::detail::Vec2> projected_points;
    projected_points.reserve(ordered_points.size());
    for (const std::uint32_t point_index : ordered_points) {
        projected_points.push_back(
            operations::detail::projectPointToPlane(positions, point_index, centroid, tangent, bitangent)
        );
    }
    return projected_points;
}

Vec3 chooseSharedLoopNormal(const MeshDocument& document, const std::vector<OrderedBoundary>& boundaries) {
    for (const OrderedBoundary& boundary : boundaries) {
        Vec3 normal = boundary.triangulation_normal;
        if (operations::detail::length(normal) <= kEpsilon) {
            normal = operations::detail::computePolygonNormal(document.positions, boundary.ordered_points);
        }
        if (operations::detail::length(normal) <= kEpsilon) {
            normal = operations::detail::findPlaneNormal(document.positions, boundary.ordered_points);
        }
        if (operations::detail::length(normal) > kEpsilon) {
            return operations::detail::normalize(normal);
        }
    }

    return Vec3{};
}

std::vector<PolygonVertex> makePolygonVertices(const ProjectedLoop& loop) {
    std::vector<PolygonVertex> polygon;
    polygon.reserve(loop.ordered_points.size());
    for (std::size_t index = 0; index < loop.ordered_points.size(); ++index) {
        polygon.push_back(PolygonVertex{
            .point_index = loop.ordered_points[index],
            .projected_point = loop.projected_points[index],
        });
    }
    return polygon;
}

void normalizeLoopOrientation(ProjectedLoop* loop, bool clockwise) {
    if (loop == nullptr) {
        return;
    }

    const bool is_clockwise = loop->signed_area < 0.0F;
    if (is_clockwise != clockwise) {
        std::reverse(loop->ordered_points.begin(), loop->ordered_points.end());
        std::reverse(loop->projected_points.begin(), loop->projected_points.end());
        loop->signed_area = -loop->signed_area;
    }
}

[[nodiscard]] bool segmentIntersectsLoopEdges(
    const operations::detail::Vec2& start,
    const operations::detail::Vec2& end,
    const std::vector<operations::detail::Vec2>& loop,
    std::size_t allowed_vertex_index
) {
    if (loop.size() < 2) {
        return false;
    }

    for (std::size_t index = 0; index < loop.size(); ++index) {
        const std::size_t next_index = (index + 1U) % loop.size();
        const operations::detail::Vec2& edge_start = loop[index];
        const operations::detail::Vec2& edge_end = loop[next_index];
        if (!operations::detail::segmentsIntersect2D(start, end, edge_start, edge_end)) {
            continue;
        }

        const bool shares_allowed_endpoint =
            allowed_vertex_index < loop.size() &&
            (index == allowed_vertex_index || next_index == allowed_vertex_index) &&
            (sameVec2(start, edge_start) || sameVec2(start, edge_end) || sameVec2(end, edge_start) ||
             sameVec2(end, edge_end));
        const bool collinear_overlap =
            std::abs(operations::detail::orient2D(start, end, edge_start)) <= kEpsilon &&
            std::abs(operations::detail::orient2D(start, end, edge_end)) <= kEpsilon;
        if (shares_allowed_endpoint && !collinear_overlap) {
            continue;
        }

        return true;
    }

    return false;
}

[[nodiscard]] bool bridgeSegmentIsValid(
    const ProjectedLoop& outer,
    const ProjectedLoop& hole,
    const std::vector<ProjectedLoop>& direct_child_holes,
    std::size_t hole_loop_index,
    std::size_t outer_vertex_index,
    std::size_t hole_vertex_index
) {
    if (outer_vertex_index >= outer.projected_points.size() || hole_vertex_index >= hole.projected_points.size()) {
        return false;
    }

    const operations::detail::Vec2& outer_point = outer.projected_points[outer_vertex_index];
    const operations::detail::Vec2& hole_point = hole.projected_points[hole_vertex_index];
    if (sameVec2(outer_point, hole_point)) {
        return false;
    }

    if (segmentIntersectsLoopEdges(hole_point, outer_point, outer.projected_points, outer_vertex_index)) {
        return false;
    }
    if (segmentIntersectsLoopEdges(hole_point, outer_point, hole.projected_points, hole_vertex_index)) {
        return false;
    }

    for (std::size_t index = 0; index < direct_child_holes.size(); ++index) {
        if (index == hole_loop_index) {
            continue;
        }
        if (segmentIntersectsLoopEdges(hole_point, outer_point, direct_child_holes[index].projected_points, kInvalidLoopIndex)) {
            return false;
        }
    }

    const operations::detail::Vec2 midpoint{
        .x = (hole_point.x + outer_point.x) * 0.5F,
        .y = (hole_point.y + outer_point.y) * 0.5F,
    };
    if (!pointInPolygon2D(outer.projected_points, midpoint)) {
        return false;
    }

    for (const ProjectedLoop& sibling_hole : direct_child_holes) {
        if (pointInPolygon2D(sibling_hole.projected_points, midpoint)) {
            return false;
        }
    }

    return true;
}

BridgeCandidate findBridgeCandidate(
    const ProjectedLoop& outer,
    const ProjectedLoop& hole,
    const std::vector<ProjectedLoop>& direct_child_holes,
    std::size_t hole_loop_index
) {
    BridgeCandidate best_candidate;
    std::vector<std::size_t> hole_vertex_indices(hole.projected_points.size());
    for (std::size_t index = 0; index < hole_vertex_indices.size(); ++index) {
        hole_vertex_indices[index] = index;
    }

    std::sort(hole_vertex_indices.begin(), hole_vertex_indices.end(), [&hole](std::size_t left, std::size_t right) {
        const operations::detail::Vec2& left_point = hole.projected_points[left];
        const operations::detail::Vec2& right_point = hole.projected_points[right];
        if (left_point.x != right_point.x) {
            return left_point.x > right_point.x;
        }
        if (left_point.y != right_point.y) {
            return left_point.y < right_point.y;
        }
        return left < right;
    });

    for (const std::size_t hole_vertex_index : hole_vertex_indices) {
        for (std::size_t outer_vertex_index = 0; outer_vertex_index < outer.projected_points.size(); ++outer_vertex_index) {
            if (!bridgeSegmentIsValid(
                    outer,
                    hole,
                    direct_child_holes,
                    hole_loop_index,
                    outer_vertex_index,
                    hole_vertex_index)) {
                continue;
            }

            const float distance_squared =
                squaredDistance2D(hole.projected_points[hole_vertex_index], outer.projected_points[outer_vertex_index]);
            if (distance_squared < best_candidate.distance_squared) {
                best_candidate = BridgeCandidate{
                    .outer_vertex_index = outer_vertex_index,
                    .hole_vertex_index = hole_vertex_index,
                    .distance_squared = distance_squared,
                };
            }
        }

        if (best_candidate.valid()) {
            break;
        }
    }

    return best_candidate;
}

std::vector<PolygonVertex> spliceHoleIntoPolygon(
    const std::vector<PolygonVertex>& polygon,
    const ProjectedLoop& hole,
    std::size_t polygon_outer_vertex_index,
    std::size_t hole_vertex_index
) {
    std::vector<PolygonVertex> merged;
    merged.reserve(polygon.size() + hole.ordered_points.size() + 2U);
    merged.insert(
        merged.end(),
        polygon.begin(),
        polygon.begin() + static_cast<std::ptrdiff_t>(polygon_outer_vertex_index + 1U)
    );

    merged.push_back(PolygonVertex{
        .point_index = hole.ordered_points[hole_vertex_index],
        .projected_point = hole.projected_points[hole_vertex_index],
    });
    for (std::size_t offset = 1; offset < hole.ordered_points.size(); ++offset) {
        const std::size_t index = (hole_vertex_index + offset) % hole.ordered_points.size();
        merged.push_back(PolygonVertex{
            .point_index = hole.ordered_points[index],
            .projected_point = hole.projected_points[index],
        });
    }
    merged.push_back(PolygonVertex{
        .point_index = hole.ordered_points[hole_vertex_index],
        .projected_point = hole.projected_points[hole_vertex_index],
    });
    merged.push_back(polygon[polygon_outer_vertex_index]);
    merged.insert(
        merged.end(),
        polygon.begin() + static_cast<std::ptrdiff_t>(polygon_outer_vertex_index + 1U),
        polygon.end()
    );

    std::vector<PolygonVertex> sanitized;
    sanitized.reserve(merged.size());
    for (const PolygonVertex& vertex : merged) {
        if (!sanitized.empty() && sameVec2(sanitized.back().projected_point, vertex.projected_point)) {
            continue;
        }
        sanitized.push_back(vertex);
    }
    if (sanitized.size() > 1U && sameVec2(sanitized.front().projected_point, sanitized.back().projected_point)) {
        sanitized.pop_back();
    }

    return sanitized;
}

[[nodiscard]] bool diagonalIntersectsPolygon(
    const operations::detail::Vec2& start,
    const operations::detail::Vec2& end,
    const std::vector<PolygonVertex>& polygon,
    std::size_t prev_index,
    std::size_t next_index
) {
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const std::size_t edge_next_index = (index + 1U) % polygon.size();
        if (index == prev_index || edge_next_index == prev_index || index == next_index || edge_next_index == next_index) {
            continue;
        }

        if (!operations::detail::segmentsIntersect2D(
                start,
                end,
                polygon[index].projected_point,
                polygon[edge_next_index].projected_point)) {
            continue;
        }

        const bool shares_start =
            sameVec2(start, polygon[index].projected_point) ||
            sameVec2(start, polygon[edge_next_index].projected_point);
        const bool shares_end =
            sameVec2(end, polygon[index].projected_point) ||
            sameVec2(end, polygon[edge_next_index].projected_point);
        const bool collinear_overlap =
            std::abs(operations::detail::orient2D(start, end, polygon[index].projected_point)) <= kEpsilon &&
            std::abs(operations::detail::orient2D(start, end, polygon[edge_next_index].projected_point)) <= kEpsilon;
        if ((shares_start || shares_end) && !collinear_overlap) {
            continue;
        }

        return true;
    }

    return false;
}

std::vector<Triangle> triangulateMergedPolygon(const std::vector<PolygonVertex>& polygon) {
    std::vector<Triangle> triangles;
    if (polygon.size() < 3U) {
        return triangles;
    }

    std::vector<PolygonVertex> working_polygon = polygon;
    std::vector<operations::detail::Vec2> working_projected_points;
    working_projected_points.reserve(working_polygon.size());
    for (const PolygonVertex& vertex : working_polygon) {
        working_projected_points.push_back(vertex.projected_point);
    }
    if (operations::detail::signedArea2D(working_projected_points) < 0.0F) {
        std::reverse(working_polygon.begin(), working_polygon.end());
    }

    triangles.reserve(working_polygon.size() - 2U);
    std::size_t guard = 0;
    while (working_polygon.size() > 3U && guard < working_polygon.size() * working_polygon.size()) {
        bool removed_ear = false;
        for (std::size_t index = 0; index < working_polygon.size(); ++index) {
            const std::size_t prev_index = (index + working_polygon.size() - 1U) % working_polygon.size();
            const std::size_t next_index = (index + 1U) % working_polygon.size();
            const operations::detail::Vec2& prev = working_polygon[prev_index].projected_point;
            const operations::detail::Vec2& current = working_polygon[index].projected_point;
            const operations::detail::Vec2& next = working_polygon[next_index].projected_point;

            if (operations::detail::orient2D(prev, current, next) <= kEpsilon) {
                continue;
            }
            if (diagonalIntersectsPolygon(prev, next, working_polygon, prev_index, next_index)) {
                continue;
            }

            bool contains_other_point = false;
            for (std::size_t point_index = 0; point_index < working_polygon.size(); ++point_index) {
                if (point_index == prev_index || point_index == index || point_index == next_index) {
                    continue;
                }

                const operations::detail::Vec2& candidate = working_polygon[point_index].projected_point;
                if (sameVec2(candidate, prev) || sameVec2(candidate, current) || sameVec2(candidate, next)) {
                    continue;
                }
                if (operations::detail::pointInTriangle2D(candidate, prev, current, next)) {
                    contains_other_point = true;
                    break;
                }
            }
            if (contains_other_point) {
                continue;
            }

            triangles.push_back(Triangle{
                .a = working_polygon[prev_index].point_index,
                .b = working_polygon[index].point_index,
                .c = working_polygon[next_index].point_index,
            });
            working_polygon.erase(working_polygon.begin() + static_cast<std::ptrdiff_t>(index));
            removed_ear = true;
            break;
        }

        if (!removed_ear) {
            logCreateFaceDebug("triangulateMergedPolygon failed to find an ear");
            return {};
        }

        ++guard;
    }

    if (working_polygon.size() == 3U) {
        triangles.push_back(Triangle{
            .a = working_polygon[0].point_index,
            .b = working_polygon[1].point_index,
            .c = working_polygon[2].point_index,
        });
    }

    return triangles;
}

std::vector<Triangle> triangulateProjectedRegion(
    const ProjectedLoop& outer,
    const std::vector<ProjectedLoop>& direct_child_holes
) {
    if (direct_child_holes.empty()) {
        return {};
    }

    std::vector<PolygonVertex> merged_polygon = makePolygonVertices(outer);
    for (std::size_t hole_index = 0; hole_index < direct_child_holes.size(); ++hole_index) {
        const BridgeCandidate bridge = findBridgeCandidate(outer, direct_child_holes[hole_index], direct_child_holes, hole_index);
        if (!bridge.valid()) {
            logCreateFaceDebug(
                "failed to find hole bridge for hole_index=" + std::to_string(hole_index) +
                " hole_area=" + std::to_string(direct_child_holes[hole_index].absolute_area)
            );
            return {};
        }

        std::size_t insertion_index = kInvalidLoopIndex;
        const std::uint32_t outer_point_index = outer.ordered_points[bridge.outer_vertex_index];
        const operations::detail::Vec2& outer_projected_point = outer.projected_points[bridge.outer_vertex_index];
        for (std::size_t polygon_index = 0; polygon_index < merged_polygon.size(); ++polygon_index) {
            if (merged_polygon[polygon_index].point_index == outer_point_index &&
                sameVec2(merged_polygon[polygon_index].projected_point, outer_projected_point)) {
                insertion_index = polygon_index;
                break;
            }
        }
        if (insertion_index == kInvalidLoopIndex) {
            logCreateFaceDebug("failed to find polygon insertion index for hole bridge");
            return {};
        }

        merged_polygon = spliceHoleIntoPolygon(
            merged_polygon,
            direct_child_holes[hole_index],
            insertion_index,
            bridge.hole_vertex_index
        );
    }

    return triangulateMergedPolygon(merged_polygon);
}

std::vector<Triangle> createFaceTrianglesForMultipleEdgeLoops(
    const MeshDocument& document,
    const operations::detail::MeshTopology& topology,
    const EntitySelection& selection
) {
    const std::vector<EntitySelection> edge_components = splitSelectedEdgeComponents(topology, selection);
    logCreateFaceDebug("edge component count=" + std::to_string(edge_components.size()));

    std::vector<OrderedBoundary> boundaries;
    boundaries.reserve(edge_components.size());
    for (const EntitySelection& component_selection : edge_components) {
        const std::optional<OrderedBoundary> boundary =
            orderBoundaryForSelection(document, topology, component_selection);
        if (!boundary.has_value()) {
            continue;
        }
        boundaries.push_back(*boundary);
    }
    if (boundaries.empty()) {
        return {};
    }
    if (boundaries.size() == 1U) {
        return operations::detail::triangulateOrderedFace(
            document.positions,
            boundaries.front().ordered_points,
            boundaries.front().triangulation_normal
        );
    }

    const Vec3 shared_normal = chooseSharedLoopNormal(document, boundaries);
    if (operations::detail::length(shared_normal) <= kEpsilon) {
        logCreateFaceDebug("multiple edge loops aborted: shared normal was zero");
        return {};
    }

    std::vector<std::uint32_t> all_loop_points;
    for (const OrderedBoundary& boundary : boundaries) {
        all_loop_points.insert(all_loop_points.end(), boundary.ordered_points.begin(), boundary.ordered_points.end());
    }
    const Vec3 projection_centroid = operations::detail::computeCentroid(document.positions, all_loop_points);
    Vec3 tangent{};
    Vec3 bitangent{};
    operations::detail::buildPlaneBasis(shared_normal, &tangent, &bitangent);

    std::vector<ProjectedLoop> loops;
    loops.reserve(boundaries.size());
    for (OrderedBoundary& boundary : boundaries) {
        if (operations::detail::length(boundary.triangulation_normal) > kEpsilon &&
            operations::detail::dot(boundary.triangulation_normal, shared_normal) < 0.0F) {
            std::reverse(boundary.ordered_points.begin(), boundary.ordered_points.end());
        }

        ProjectedLoop loop{
            .ordered_points = boundary.ordered_points,
            .projected_points = projectOrderedPoints(
                document.positions,
                boundary.ordered_points,
                projection_centroid,
                tangent,
                bitangent
            ),
        };
        loop.signed_area = operations::detail::signedArea2D(loop.projected_points);
        loop.absolute_area = std::abs(loop.signed_area);
        if (loop.absolute_area <= kEpsilon) {
            continue;
        }
        loops.push_back(std::move(loop));
    }
    if (loops.empty()) {
        return {};
    }

    for (std::size_t loop_index = 0; loop_index < loops.size(); ++loop_index) {
        const operations::detail::Vec2 sample_point = loops[loop_index].projected_points.front();
        float best_parent_area = std::numeric_limits<float>::infinity();
        for (std::size_t candidate_index = 0; candidate_index < loops.size(); ++candidate_index) {
            if (candidate_index == loop_index || loops[candidate_index].absolute_area <= loops[loop_index].absolute_area) {
                continue;
            }
            if (!pointInPolygon2D(loops[candidate_index].projected_points, sample_point)) {
                continue;
            }

            if (loops[candidate_index].absolute_area < best_parent_area) {
                best_parent_area = loops[candidate_index].absolute_area;
                loops[loop_index].parent_index = candidate_index;
            }
        }
    }

    for (ProjectedLoop& loop : loops) {
        std::size_t depth = 0;
        for (std::size_t parent_index = loop.parent_index;
             parent_index != kInvalidLoopIndex;
             parent_index = loops[parent_index].parent_index) {
            ++depth;
        }
        loop.depth = depth;
    }

    std::vector<Triangle> created_triangles;
    for (std::size_t loop_index = 0; loop_index < loops.size(); ++loop_index) {
        if ((loops[loop_index].depth % 2U) != 0U) {
            continue;
        }

        ProjectedLoop outer = loops[loop_index];
        normalizeLoopOrientation(&outer, false);

        std::vector<ProjectedLoop> direct_child_holes;
        for (std::size_t child_index = 0; child_index < loops.size(); ++child_index) {
            if (loops[child_index].parent_index != loop_index || (loops[child_index].depth % 2U) == 0U) {
                continue;
            }

            ProjectedLoop hole = loops[child_index];
            normalizeLoopOrientation(&hole, true);
            direct_child_holes.push_back(std::move(hole));
        }

        std::vector<Triangle> region_triangles;
        if (direct_child_holes.empty()) {
            region_triangles = operations::detail::triangulateOrderedFace(
                document.positions,
                outer.ordered_points,
                shared_normal
            );
        } else {
            region_triangles = triangulateProjectedRegion(outer, direct_child_holes);
        }
        if (region_triangles.empty()) {
            logCreateFaceDebug(
                "failed to triangulate loop_index=" + std::to_string(loop_index) +
                " outer_area=" + std::to_string(outer.absolute_area) +
                " hole_count=" + std::to_string(direct_child_holes.size())
            );
            continue;
        }

        created_triangles.insert(
            created_triangles.end(),
            region_triangles.begin(),
            region_triangles.end()
        );
    }

    return created_triangles;
}

}  // namespace

ModifyCreateFaceAvailability computeModifyCreateFaceAvailability(
    const MeshDocument& document,
    const EntitySelection& selection
) {
    const operations::detail::MeshTopology topology = operations::detail::buildMeshTopology(document);
    return computeModifyCreateFaceAvailability(document, topology, selection);
}

ModifyCreateFaceAvailability computeModifyCreateFaceAvailability(
    const MeshDocument& document,
    const operations::detail::MeshTopology& topology,
    const EntitySelection& selection
) {
    ModifyCreateFaceAvailability availability;
    for (const std::uint32_t point_index : selection.point_indices) {
        if (point_index < document.positions.size()) {
            ++availability.point_count;
        }
    }

    for (const std::uint32_t edge_index : selection.edge_indices) {
        if (edge_index < topology.edge_points.size()) {
            ++availability.edge_count;
        }
    }

    availability.candidate_point_count = operations::detail::collectPointsFromSelectedEdges(topology, selection).size();
    return availability;
}

ModifyCreateFaceResult applyModifyCreateFace(MeshDocument* document, const EntitySelection& selection) {
    ModifyCreateFaceResult result;
    if (document == nullptr) {
        logCreateFaceDebug("applyModifyCreateFace aborted: document was null");
        return result;
    }

    ensureRenderableNormals(document);

    const operations::detail::MeshTopology topology = operations::detail::buildMeshTopology(*document);
    const ModifyCreateFaceAvailability availability = computeModifyCreateFaceAvailability(*document, topology, selection);
    logCreateFaceDebug(
        "start positions=" + std::to_string(document->positions.size()) +
        " triangles=" + std::to_string(document->triangles.size()) +
        " selected_points=" + std::to_string(selection.point_indices.size()) +
        " selected_edges=" + std::to_string(selection.edge_indices.size()) +
        " point_indices=" + formatIndices(selection.point_indices) +
        " edge_indices=" + formatIndices(selection.edge_indices) +
        " availability(point_count=" + std::to_string(availability.point_count) +
        ", edge_count=" + std::to_string(availability.edge_count) +
        ", candidate_point_count=" + std::to_string(availability.candidate_point_count) + ")"
    );
    std::vector<Triangle> created_triangles;
    if (availability.point_count >= 3) {
        created_triangles = createFaceTrianglesForSelection(*document, topology, selection);
    } else {
        created_triangles = createFaceTrianglesForMultipleEdgeLoops(*document, topology, selection);
    }

    if (created_triangles.empty()) {
        logCreateFaceDebug("createFaceTrianglesForSelection returned 0 triangles");
        return result;
    }

    const std::uint32_t first_face_index = static_cast<std::uint32_t>(document->triangles.size());
    document->triangles.insert(document->triangles.end(), created_triangles.begin(), created_triangles.end());
    logCreateFaceDebug(
        "success created_face_count=" + std::to_string(created_triangles.size()) +
        " first_face_index=" + std::to_string(first_face_index)
    );

    result.changed = true;
    result.created_face_count = created_triangles.size();
    result.created_face_indices.reserve(created_triangles.size());
    for (std::uint32_t index = 0; index < result.created_face_count; ++index) {
        result.created_face_indices.push_back(first_face_index + index);
    }

    return result;
}

}  // namespace meshtools::mesh
