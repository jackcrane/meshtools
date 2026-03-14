#include "meshtools/mesh/MeshOperations/Detail.h"

#include <cmath>

namespace meshtools::mesh::operations::detail {

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

bool linePlaneIntersection(const Vec3& line_point, const Vec3& line_direction, const Plane& plane, float* parameter_out) {
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
    const float denominator = (first_dot_first * second_dot_second) - (first_dot_second * first_dot_second);
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

}  // namespace meshtools::mesh::operations::detail
