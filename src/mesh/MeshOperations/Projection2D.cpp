#include "meshtools/mesh/MeshOperations/Detail.h"

namespace meshtools::mesh::operations::detail {

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

}  // namespace meshtools::mesh::operations::detail
