#include "meshtools/mesh/MeshOperations/Detail.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <tuple>

namespace meshtools::mesh::operations::detail {
namespace {

constexpr double kWeldPrecision = 1000000.0;

}

std::size_t QuantizedPositionKeyHash::operator()(const QuantizedPositionKey& key) const noexcept {
    const std::size_t hx = std::hash<std::int64_t>{}(key.x);
    const std::size_t hy = std::hash<std::int64_t>{}(key.y);
    const std::size_t hz = std::hash<std::int64_t>{}(key.z);
    return hx ^ (hy << 1U) ^ (hz << 2U);
}

std::size_t QuantizedEdgeKeyHash::operator()(const QuantizedEdgeKey& key) const noexcept {
    const std::size_t ha = QuantizedPositionKeyHash{}(key.a);
    const std::size_t hb = QuantizedPositionKeyHash{}(key.b);
    return ha ^ (hb << 1U);
}

bool lessThan(const QuantizedPositionKey& left, const QuantizedPositionKey& right) {
    if (left.x != right.x) {
        return left.x < right.x;
    }
    if (left.y != right.y) {
        return left.y < right.y;
    }
    return left.z < right.z;
}

QuantizedPositionKey makeQuantizedPositionKey(const Vec3& position) {
    return QuantizedPositionKey{
        .x = static_cast<std::int64_t>(std::llround(static_cast<double>(position.x) * kWeldPrecision)),
        .y = static_cast<std::int64_t>(std::llround(static_cast<double>(position.y) * kWeldPrecision)),
        .z = static_cast<std::int64_t>(std::llround(static_cast<double>(position.z) * kWeldPrecision)),
    };
}

QuantizedEdgeKey makeQuantizedEdgeKey(QuantizedPositionKey first, QuantizedPositionKey second) {
    if (lessThan(second, first)) {
        std::swap(first, second);
    }

    return QuantizedEdgeKey{
        .a = first,
        .b = second,
    };
}

Bounds calculateBounds(const std::vector<Vec3>& positions) {
    Bounds bounds;
    if (positions.empty()) {
        return bounds;
    }

    bounds.minimum = positions.front();
    bounds.maximum = positions.front();
    bounds.valid = true;

    for (const Vec3& position : positions) {
        bounds.minimum.x = std::min(bounds.minimum.x, position.x);
        bounds.minimum.y = std::min(bounds.minimum.y, position.y);
        bounds.minimum.z = std::min(bounds.minimum.z, position.z);
        bounds.maximum.x = std::max(bounds.maximum.x, position.x);
        bounds.maximum.y = std::max(bounds.maximum.y, position.y);
        bounds.maximum.z = std::max(bounds.maximum.z, position.z);
    }

    return bounds;
}

Vec3 subtract(const Vec3& left, const Vec3& right) {
    return Vec3{
        .x = left.x - right.x,
        .y = left.y - right.y,
        .z = left.z - right.z,
    };
}

Vec3 cross(const Vec3& left, const Vec3& right) {
    return Vec3{
        .x = (left.y * right.z) - (left.z * right.y),
        .y = (left.z * right.x) - (left.x * right.z),
        .z = (left.x * right.y) - (left.y * right.x),
    };
}

Vec3 add(const Vec3& left, const Vec3& right) {
    return Vec3{
        .x = left.x + right.x,
        .y = left.y + right.y,
        .z = left.z + right.z,
    };
}

float length(const Vec3& value) {
    return std::sqrt((value.x * value.x) + (value.y * value.y) + (value.z * value.z));
}

float dot(const Vec3& left, const Vec3& right) {
    return (left.x * right.x) + (left.y * right.y) + (left.z * right.z);
}

Vec3 scale(const Vec3& value, float factor) {
    return Vec3{
        .x = value.x * factor,
        .y = value.y * factor,
        .z = value.z * factor,
    };
}

Vec3 normalize(const Vec3& value) {
    const float magnitude = length(value);
    if (magnitude <= 1.0e-8F) {
        return Vec3{};
    }

    return Vec3{
        .x = value.x / magnitude,
        .y = value.y / magnitude,
        .z = value.z / magnitude,
    };
}

std::vector<Vec3> calculateVertexNormals(const std::vector<Vec3>& positions, const std::vector<Triangle>& triangles) {
    std::vector<Vec3> normals(positions.size(), Vec3{});
    for (const Triangle& triangle : triangles) {
        if (triangle.a >= positions.size() || triangle.b >= positions.size() || triangle.c >= positions.size()) {
            continue;
        }

        const Vec3 edge_ab = subtract(positions[triangle.b], positions[triangle.a]);
        const Vec3 edge_ac = subtract(positions[triangle.c], positions[triangle.a]);
        const Vec3 face_normal = normalize(cross(edge_ab, edge_ac));

        normals[triangle.a] = add(normals[triangle.a], face_normal);
        normals[triangle.b] = add(normals[triangle.b], face_normal);
        normals[triangle.c] = add(normals[triangle.c], face_normal);
    }

    for (Vec3& normal : normals) {
        normal = normalize(normal);
    }

    return normals;
}

Vec3 rotateXAxisNegative90(const Vec3& value) {
    return Vec3{
        .x = value.x,
        .y = value.z,
        .z = -value.y,
    };
}

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
            const QuantizedEdgeKey& edge_key = edge_keys[edge_offset];
            const auto [iterator, inserted] =
                topology.edge_index_by_key.emplace(edge_key, static_cast<std::uint32_t>(topology.unique_edge_keys.size()));
            if (inserted) {
                topology.unique_edge_keys.push_back(edge_key);
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

        const QuantizedEdgeKey edge_key = makeQuantizedEdgeKey(topology.point_keys[edge.a], topology.point_keys[edge.b]);
        const auto [iterator, inserted] =
            topology.edge_index_by_key.emplace(edge_key, static_cast<std::uint32_t>(topology.unique_edge_keys.size()));
        if (inserted) {
            topology.unique_edge_keys.push_back(edge_key);
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
        if (edge_index >= topology.edge_points.size()) {
            continue;
        }

        point_indices.push_back(topology.edge_points[edge_index].a);
        point_indices.push_back(topology.edge_points[edge_index].b);
    }

    return uniqueSortedIndices(std::move(point_indices));
}

Vec3 chooseReferenceAxis(const Vec3& normal) {
    const float x_alignment = std::fabs(dot(normal, Vec3{1.0F, 0.0F, 0.0F}));
    if (x_alignment < 0.9F) {
        return Vec3{1.0F, 0.0F, 0.0F};
    }
    return Vec3{0.0F, 1.0F, 0.0F};
}

void buildPlaneBasis(const Vec3& normal, Vec3* tangent, Vec3* bitangent) {
    const Vec3 reference_axis = chooseReferenceAxis(normal);
    *tangent = normalize(cross(reference_axis, normal));
    *bitangent = normalize(cross(normal, *tangent));
}

Vec3 computeTriangleNormal(const std::vector<Vec3>& positions, const Triangle& triangle) {
    if (triangle.a >= positions.size() || triangle.b >= positions.size() || triangle.c >= positions.size()) {
        return Vec3{};
    }

    const Vec3 edge_ab = subtract(positions[triangle.b], positions[triangle.a]);
    const Vec3 edge_ac = subtract(positions[triangle.c], positions[triangle.a]);
    return normalize(cross(edge_ab, edge_ac));
}

float lengthSquared(const Vec3& value) {
    return dot(value, value);
}

bool samePosition(const Vec3& first, const Vec3& second, float tolerance) {
    return lengthSquared(subtract(first, second)) <= (tolerance * tolerance);
}

Plane makeTrianglePlane(const std::vector<Vec3>& positions, const Triangle& triangle) {
    Plane plane;
    if (triangle.a >= positions.size() || triangle.b >= positions.size() || triangle.c >= positions.size()) {
        return plane;
    }

    plane.normal = computeTriangleNormal(positions, triangle);
    if (length(plane.normal) <= 1.0e-6F) {
        return plane;
    }

    plane.distance = dot(plane.normal, positions[triangle.a]);
    plane.valid = true;
    return plane;
}

float modelDiagonalLength(const MeshDocument& document) {
    if (!document.bounds.valid) {
        return 1.0F;
    }
    return length(subtract(document.bounds.maximum, document.bounds.minimum));
}

bool intersectPlanes(const Plane& first, const Plane& second, Vec3* point, Vec3* direction) {
    if (!first.valid || !second.valid || point == nullptr || direction == nullptr) {
        return false;
    }

    *direction = cross(first.normal, second.normal);
    const float direction_length_squared = dot(*direction, *direction);
    if (direction_length_squared <= 1.0e-8F) {
        return false;
    }

    const Vec3 term_a = scale(cross(second.normal, *direction), first.distance);
    const Vec3 term_b = scale(cross(*direction, first.normal), second.distance);
    *point = scale(add(term_a, term_b), 1.0F / direction_length_squared);
    *direction = normalize(*direction);
    return true;
}

bool linePlaneIntersection(
    const Vec3& line_point,
    const Vec3& line_direction,
    const Plane& plane,
    float* parameter_out
) {
    if (!plane.valid || parameter_out == nullptr) {
        return false;
    }

    const float denominator = dot(plane.normal, line_direction);
    if (std::fabs(denominator) <= 1.0e-6F) {
        return false;
    }

    *parameter_out = (plane.distance - dot(plane.normal, line_point)) / denominator;
    return true;
}

ClosestLinePoints closestPointsBetweenLines(
    const Vec3& first_point,
    const Vec3& first_direction,
    const Vec3& second_point,
    const Vec3& second_direction
) {
    const Vec3 delta = subtract(first_point, second_point);
    const float first_dot_first = dot(first_direction, first_direction);
    const float first_dot_second = dot(first_direction, second_direction);
    const float second_dot_second = dot(second_direction, second_direction);
    const float first_dot_delta = dot(first_direction, delta);
    const float second_dot_delta = dot(second_direction, delta);
    const float denominator =
        (first_dot_first * second_dot_second) - (first_dot_second * first_dot_second);
    if (std::abs(denominator) <= 1.0e-6F) {
        return {};
    }

    const float first_parameter =
        ((first_dot_second * second_dot_delta) - (second_dot_second * first_dot_delta)) / denominator;
    const float second_parameter =
        ((first_dot_first * second_dot_delta) - (first_dot_second * first_dot_delta)) / denominator;
    const Vec3 closest_first = add(first_point, scale(first_direction, first_parameter));
    const Vec3 closest_second = add(second_point, scale(second_direction, second_parameter));
    return ClosestLinePoints{
        .valid = true,
        .first_parameter = first_parameter,
        .second_parameter = second_parameter,
        .first_point = closest_first,
        .second_point = closest_second,
        .distance = length(subtract(closest_first, closest_second)),
    };
}

Plane planeFromFace(const MeshDocument& document, std::uint32_t face_index) {
    if (face_index >= document.triangles.size()) {
        return {};
    }
    return makeTrianglePlane(document.positions, document.triangles[face_index]);
}

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
            if (!linePlaneIntersection(line_point, line_direction, plane, &parameter)) {
                return std::nullopt;
            }
            return parameter;
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
            const ClosestLinePoints closest = closestPointsBetweenLines(
                line_point,
                line_direction,
                edge_origin,
                edge_direction
            );
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
            const Vec3 offset = subtract(point, line_point);
            const float parameter = dot(offset, line_direction);
            const Vec3 projected = add(line_point, scale(line_direction, parameter));
            if (length(subtract(projected, point)) > tolerance) {
                return std::nullopt;
            }
            return parameter;
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
    if (iterator == topology.edge_index_by_key.end()) {
        return std::nullopt;
    }
    return iterator->second;
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
            if (point_index >= topology.face_indices_by_point.size()) {
                continue;
            }
            face_indices.insert(
                face_indices.end(),
                topology.face_indices_by_point[point_index].begin(),
                topology.face_indices_by_point[point_index].end()
            );
        }
    } else {
        for (const std::uint32_t edge_index : selection.edge_indices) {
            if (edge_index >= topology.face_indices_by_edge.size()) {
                continue;
            }
            face_indices.insert(
                face_indices.end(),
                topology.face_indices_by_edge[edge_index].begin(),
                topology.face_indices_by_edge[edge_index].end()
            );
        }
    }

    sortAndUnique(face_indices);
    Vec3 accumulated_normal{};
    for (const std::uint32_t face_index : face_indices) {
        if (face_index >= document.triangles.size()) {
            continue;
        }
        accumulated_normal = add(accumulated_normal, computeTriangleNormal(document.positions, document.triangles[face_index]));
    }
    return normalize(accumulated_normal);
}

Vec3 computeCentroid(const std::vector<Vec3>& positions, const std::vector<std::uint32_t>& point_indices) {
    Vec3 centroid{};
    std::size_t valid_count = 0;
    for (const std::uint32_t point_index : point_indices) {
        if (point_index >= positions.size()) {
            continue;
        }
        centroid = add(centroid, positions[point_index]);
        ++valid_count;
    }

    if (valid_count == 0) {
        return centroid;
    }

    centroid.x /= static_cast<float>(valid_count);
    centroid.y /= static_cast<float>(valid_count);
    centroid.z /= static_cast<float>(valid_count);
    return centroid;
}

Vec2 projectPointToPlane(
    const std::vector<Vec3>& positions,
    std::uint32_t point_index,
    const Vec3& centroid,
    const Vec3& tangent,
    const Vec3& bitangent
) {
    if (point_index >= positions.size()) {
        return Vec2{};
    }

    const Vec3 offset = subtract(positions[point_index], centroid);
    return Vec2{
        .x = dot(offset, tangent),
        .y = dot(offset, bitangent),
    };
}

float signedArea2D(const std::vector<Vec2>& points) {
    if (points.size() < 3) {
        return 0.0F;
    }

    float area = 0.0F;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const Vec2& current = points[index];
        const Vec2& next = points[(index + 1U) % points.size()];
        area += (current.x * next.y) - (next.x * current.y);
    }
    return area * 0.5F;
}

float orient2D(const Vec2& a, const Vec2& b, const Vec2& c) {
    return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
}

bool pointInTriangle2D(const Vec2& point, const Vec2& a, const Vec2& b, const Vec2& c) {
    const float ab = orient2D(a, b, point);
    const float bc = orient2D(b, c, point);
    const float ca = orient2D(c, a, point);
    const bool has_negative = ab < -1.0e-6F || bc < -1.0e-6F || ca < -1.0e-6F;
    const bool has_positive = ab > 1.0e-6F || bc > 1.0e-6F || ca > 1.0e-6F;
    return !(has_negative && has_positive);
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

    Vec3 normal = preferred_normal;
    if (length(normal) <= 1.0e-6F) {
        normal = findPlaneNormal(positions, point_indices);
    }
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
            triangles.reserve(ordered_points.size() - 2U);
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

void remapExplicitEdges(
    std::vector<EdgeSegment>* edges,
    const std::vector<std::uint32_t>& old_to_new_point
) {
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
        if (new_a == kInvalidIndex || new_b == kInvalidIndex || new_a == new_b) {
            continue;
        }

        remapped.push_back(EdgeSegment{.a = new_a, .b = new_b});
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
