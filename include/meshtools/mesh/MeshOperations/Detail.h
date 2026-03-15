#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

#include "meshtools/mesh/MeshDocument.h"
#include "meshtools/mesh/MeshOperations/ProjectEdge.h"

namespace meshtools::mesh::operations::detail {

inline constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();

struct QuantizedPositionKey {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;

    [[nodiscard]] bool operator==(const QuantizedPositionKey& other) const = default;
};

struct QuantizedPositionKeyHash {
    [[nodiscard]] std::size_t operator()(const QuantizedPositionKey& key) const noexcept;
};

struct QuantizedEdgeKey {
    QuantizedPositionKey a;
    QuantizedPositionKey b;

    [[nodiscard]] bool operator==(const QuantizedEdgeKey& other) const = default;
};

struct QuantizedEdgeKeyHash {
    [[nodiscard]] std::size_t operator()(const QuantizedEdgeKey& key) const noexcept;
};

struct MeshTopology {
    struct EdgePoints {
        std::uint32_t a = 0;
        std::uint32_t b = 0;
    };

    std::vector<QuantizedPositionKey> point_keys;
    std::vector<QuantizedEdgeKey> unique_edge_keys;
    std::vector<EdgePoints> edge_points;
    std::vector<bool> edge_is_explicit;
    std::vector<std::uint32_t> explicit_edge_document_indices;
    std::vector<std::vector<std::uint32_t>> face_indices_by_edge;
    std::vector<std::vector<std::uint32_t>> face_indices_by_point;
    std::unordered_map<QuantizedEdgeKey, std::uint32_t, QuantizedEdgeKeyHash> edge_index_by_key;
};

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct Plane {
    Vec3 normal;
    float distance = 0.0F;
    bool valid = false;
};

struct ClosestLinePoints {
    bool valid = false;
    float first_parameter = 0.0F;
    float second_parameter = 0.0F;
    Vec3 first_point;
    Vec3 second_point;
    float distance = 0.0F;
};

[[nodiscard]] bool lessThan(const QuantizedPositionKey& left, const QuantizedPositionKey& right);
[[nodiscard]] QuantizedPositionKey makeQuantizedPositionKey(const Vec3& position);
[[nodiscard]] QuantizedEdgeKey makeQuantizedEdgeKey(QuantizedPositionKey first, QuantizedPositionKey second);
[[nodiscard]] Bounds calculateBounds(const std::vector<Vec3>& positions);
[[nodiscard]] Vec3 subtract(const Vec3& left, const Vec3& right);
[[nodiscard]] Vec3 cross(const Vec3& left, const Vec3& right);
[[nodiscard]] Vec3 add(const Vec3& left, const Vec3& right);
[[nodiscard]] float length(const Vec3& value);
[[nodiscard]] float dot(const Vec3& left, const Vec3& right);
[[nodiscard]] Vec3 scale(const Vec3& value, float factor);
[[nodiscard]] Vec3 normalize(const Vec3& value);
[[nodiscard]] std::vector<Vec3> calculateVertexNormals(
    const std::vector<Vec3>& positions,
    const std::vector<Triangle>& triangles
);
[[nodiscard]] Vec3 rotateXAxisNegative90(const Vec3& value);
[[nodiscard]] std::vector<Vec3> normalizePositionsForTopology(const MeshDocument& document);
void sortAndUnique(std::vector<std::uint32_t>& indices);
[[nodiscard]] std::vector<std::uint32_t> uniqueSortedIndices(std::vector<std::uint32_t> indices);
[[nodiscard]] MeshTopology buildMeshTopology(const MeshDocument& document);
[[nodiscard]] std::vector<std::uint32_t> collectPointsFromSelectedEdges(
    const MeshTopology& topology,
    const EntitySelection& selection
);
[[nodiscard]] Vec3 chooseReferenceAxis(const Vec3& normal);
void buildPlaneBasis(const Vec3& normal, Vec3* tangent, Vec3* bitangent);
[[nodiscard]] Vec3 computeTriangleNormal(const std::vector<Vec3>& positions, const Triangle& triangle);
[[nodiscard]] float lengthSquared(const Vec3& value);
[[nodiscard]] bool samePosition(const Vec3& first, const Vec3& second, float tolerance = 1.0e-5F);
[[nodiscard]] Plane makeTrianglePlane(const std::vector<Vec3>& positions, const Triangle& triangle);
[[nodiscard]] float modelDiagonalLength(const MeshDocument& document);
[[nodiscard]] bool intersectPlanes(const Plane& first, const Plane& second, Vec3* point, Vec3* direction);
[[nodiscard]] bool linePlaneIntersection(
    const Vec3& line_point,
    const Vec3& line_direction,
    const Plane& plane,
    float* parameter_out
);
[[nodiscard]] ClosestLinePoints closestPointsBetweenLines(
    const Vec3& first_point,
    const Vec3& first_direction,
    const Vec3& second_point,
    const Vec3& second_direction
);
[[nodiscard]] Plane planeFromFace(const MeshDocument& document, std::uint32_t face_index);
[[nodiscard]] std::optional<float> resolveProjectTargetParameter(
    const MeshDocument& document,
    const MeshTopology& topology,
    const ModifyProjectTarget& target,
    const Vec3& line_point,
    const Vec3& line_direction,
    float tolerance
);
[[nodiscard]] std::optional<std::uint32_t> findTopologyEdgeIndexForPoints(
    const MeshTopology& topology,
    const std::vector<Vec3>& normalized_positions,
    std::uint32_t first_point_index,
    std::uint32_t second_point_index
);
[[nodiscard]] std::vector<std::uint32_t> selectedFaceIndices(
    const MeshDocument& document,
    const EntitySelection& selection
);
[[nodiscard]] std::uint32_t findOrAppendPointIndex(MeshDocument* document, const Vec3& position);
[[nodiscard]] Vec3 computePolygonNormal(
    const std::vector<Vec3>& positions,
    const std::vector<std::uint32_t>& ordered_points
);
[[nodiscard]] Vec3 computeReferenceNormal(
    const MeshDocument& document,
    const MeshTopology& topology,
    const EntitySelection& selection,
    bool from_points
);
[[nodiscard]] Vec3 computeCentroid(
    const std::vector<Vec3>& positions,
    const std::vector<std::uint32_t>& point_indices
);
[[nodiscard]] Vec2 projectPointToPlane(
    const std::vector<Vec3>& positions,
    std::uint32_t point_index,
    const Vec3& centroid,
    const Vec3& tangent,
    const Vec3& bitangent
);
[[nodiscard]] float signedArea2D(const std::vector<Vec2>& points);
[[nodiscard]] float orient2D(const Vec2& a, const Vec2& b, const Vec2& c);
[[nodiscard]] bool pointInTriangle2D(const Vec2& point, const Vec2& a, const Vec2& b, const Vec2& c);
[[nodiscard]] bool segmentsIntersect2D(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d);
[[nodiscard]] bool untanglePolygonOrder(
    std::vector<std::uint32_t>* ordered_points,
    std::vector<Vec2>* projected_points
);
[[nodiscard]] Vec3 findPlaneNormal(
    const std::vector<Vec3>& positions,
    const std::vector<std::uint32_t>& point_indices
);
[[nodiscard]] std::vector<std::uint32_t> sortPointsForFace(
    const std::vector<Vec3>& positions,
    std::vector<std::uint32_t> point_indices,
    Vec3 preferred_normal = Vec3{}
);
[[nodiscard]] std::vector<std::uint32_t> orderPointsFromConnectedPoints(
    const MeshTopology& topology,
    const std::vector<Vec3>& positions,
    std::vector<std::uint32_t> point_indices,
    Vec3 preferred_normal = Vec3{}
);
[[nodiscard]] std::vector<std::uint32_t> orderPointsFromEdges(
    const MeshTopology& topology,
    const std::vector<Vec3>& positions,
    const EntitySelection& selection,
    Vec3 preferred_normal = Vec3{}
);
[[nodiscard]] std::vector<Triangle> triangulateOrderedFace(
    const std::vector<Vec3>& positions,
    std::vector<std::uint32_t> ordered_points,
    Vec3 preferred_normal
);
void remapIndexVector(std::vector<std::uint32_t>* indices, const std::vector<std::uint32_t>& old_to_new);
void remapExplicitEdges(
    std::vector<EdgeSegment>* edges,
    const std::vector<std::uint32_t>& old_to_new_point
);
void remapEdgeIndexVector(
    std::vector<std::uint32_t>* indices,
    const MeshTopology& old_topology,
    const MeshTopology& new_topology
);

}  // namespace meshtools::mesh::operations::detail
