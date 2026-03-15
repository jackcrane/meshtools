#include "meshtools/mesh/MeshOperations/Detail.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>

namespace meshtools::mesh::operations::detail {

namespace {

constexpr float kIntersectionEpsilon = 1.0e-6F;

[[nodiscard]] bool pointOnSegment2D(const Vec2& point, const Vec2& start, const Vec2& end) {
    if (std::abs(orient2D(start, end, point)) > kIntersectionEpsilon) {
        return false;
    }

    const float min_x = std::min(start.x, end.x) - kIntersectionEpsilon;
    const float max_x = std::max(start.x, end.x) + kIntersectionEpsilon;
    const float min_y = std::min(start.y, end.y) - kIntersectionEpsilon;
    const float max_y = std::max(start.y, end.y) + kIntersectionEpsilon;
    return point.x >= min_x && point.x <= max_x && point.y >= min_y && point.y <= max_y;
}

[[nodiscard]] bool areAdjacentEdges(std::size_t first, std::size_t second, std::size_t count) {
    if (first == second) {
        return true;
    }

    const std::size_t first_next = (first + 1U) % count;
    const std::size_t second_next = (second + 1U) % count;
    return first == second_next || first_next == second;
}

void logProjectionDebug(const std::string& message) {
    std::cout << "[FACEDBG] " << message << std::endl;
}

}  // namespace

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

bool segmentsIntersect2D(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) {
    const float ab_c = orient2D(a, b, c);
    const float ab_d = orient2D(a, b, d);
    const float cd_a = orient2D(c, d, a);
    const float cd_b = orient2D(c, d, b);

    const bool ab_straddles =
        (ab_c > kIntersectionEpsilon && ab_d < -kIntersectionEpsilon) ||
        (ab_c < -kIntersectionEpsilon && ab_d > kIntersectionEpsilon);
    const bool cd_straddles =
        (cd_a > kIntersectionEpsilon && cd_b < -kIntersectionEpsilon) ||
        (cd_a < -kIntersectionEpsilon && cd_b > kIntersectionEpsilon);
    if (ab_straddles && cd_straddles) {
        return true;
    }

    return (std::abs(ab_c) <= kIntersectionEpsilon && pointOnSegment2D(c, a, b)) ||
           (std::abs(ab_d) <= kIntersectionEpsilon && pointOnSegment2D(d, a, b)) ||
           (std::abs(cd_a) <= kIntersectionEpsilon && pointOnSegment2D(a, c, d)) ||
           (std::abs(cd_b) <= kIntersectionEpsilon && pointOnSegment2D(b, c, d));
}

bool untanglePolygonOrder(
    std::vector<std::uint32_t>* ordered_points,
    std::vector<Vec2>* projected_points
) {
    if (ordered_points == nullptr || projected_points == nullptr) {
        logProjectionDebug("untanglePolygonOrder aborted: null input");
        return false;
    }
    if (ordered_points->size() != projected_points->size() || ordered_points->size() < 3) {
        logProjectionDebug(
            "untanglePolygonOrder aborted: ordered_points=" + std::to_string(ordered_points->size()) +
            " projected_points=" + std::to_string(projected_points->size())
        );
        return false;
    }

    const std::size_t point_count = ordered_points->size();
    const std::size_t max_iterations = point_count * point_count;
    for (std::size_t iteration = 0; iteration < max_iterations; ++iteration) {
        bool changed = false;
        for (std::size_t first = 0; first < point_count; ++first) {
            const std::size_t first_next = (first + 1U) % point_count;
            for (std::size_t second = first + 1U; second < point_count; ++second) {
                if (areAdjacentEdges(first, second, point_count)) {
                    continue;
                }

                const std::size_t second_next = (second + 1U) % point_count;
                if (!segmentsIntersect2D(
                        (*projected_points)[first],
                        (*projected_points)[first_next],
                        (*projected_points)[second],
                        (*projected_points)[second_next])) {
                    continue;
                }

                std::reverse(ordered_points->begin() + static_cast<std::ptrdiff_t>(first_next),
                             ordered_points->begin() + static_cast<std::ptrdiff_t>(second + 1U));
                std::reverse(projected_points->begin() + static_cast<std::ptrdiff_t>(first_next),
                             projected_points->begin() + static_cast<std::ptrdiff_t>(second + 1U));
                logProjectionDebug(
                    "untanglePolygonOrder swapped segment first=" + std::to_string(first) +
                    " first_next=" + std::to_string(first_next) +
                    " second=" + std::to_string(second) +
                    " second_next=" + std::to_string(second_next)
                );
                changed = true;
                break;
            }
            if (changed) {
                break;
            }
        }

        if (!changed) {
            logProjectionDebug("untanglePolygonOrder finished after iterations=" + std::to_string(iteration + 1U));
            return true;
        }
    }

    logProjectionDebug("untanglePolygonOrder gave up after max_iterations");
    return false;
}

}  // namespace meshtools::mesh::operations::detail
