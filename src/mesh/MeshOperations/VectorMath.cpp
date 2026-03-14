#include "meshtools/mesh/MeshOperations/Detail.h"

#include <algorithm>
#include <cmath>

namespace meshtools::mesh::operations::detail {

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

}  // namespace meshtools::mesh::operations::detail
