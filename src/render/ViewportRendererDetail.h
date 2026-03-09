#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "meshtools/mesh/MeshDocument.h"
#include "meshtools/render/ViewportRenderer.h"

namespace meshtools::render::detail {

inline constexpr float kVerticalFovRadians = 0.85F;
inline constexpr float kNearPlane = 0.1F;
inline constexpr float kFarPlane = 10.0F;
inline constexpr float kDefaultPointSize = 7.0F;
inline constexpr float kSelectionPointSize = 11.0F;
inline constexpr float kFaceDepthBias = 0.0012F;
inline constexpr float kEdgeDepthBias = 0.0018F;
inline constexpr float kPointDepthBias = 0.0022F;
inline constexpr float kEdgeHitRadiusPixels = 8.0F;
inline constexpr float kPointHitRadiusPixels = 11.0F;
inline constexpr float kSelectionDepthTolerance = 0.03F;
inline constexpr float kDepthBufferVisibilityEpsilon = 0.0025F;
inline constexpr float kIntersectionEpsilon = 0.00001F;

struct CameraData {
    mesh::Vec3 eye;
    mesh::Vec3 right;
    mesh::Vec3 up;
    mesh::Vec3 forward;
};

struct HitCandidate {
    bool hit = false;
    std::uint32_t index = 0;
    float t = 0.0F;
};

struct ProjectedPoint {
    bool valid = false;
    float normalized_x = 0.0F;
    float normalized_y = 0.0F;
    float view_depth = 0.0F;
    float depth_buffer_value = 1.0F;
};

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

mesh::Vec3 add(const mesh::Vec3& left, const mesh::Vec3& right);
mesh::Vec3 subtract(const mesh::Vec3& left, const mesh::Vec3& right);
mesh::Vec3 scale(const mesh::Vec3& value, float factor);
float length(const mesh::Vec3& value);
float dot(const mesh::Vec3& left, const mesh::Vec3& right);
mesh::Vec3 normalize(const mesh::Vec3& value);
mesh::Vec3 cross(const mesh::Vec3& left, const mesh::Vec3& right);
mesh::Vec3 rotateXAxisNegative90(const mesh::Vec3& value);

std::uint32_t createShaderProgram();
std::uint32_t createAxisShaderProgram();
std::uint32_t createHighlightShaderProgram();

std::array<float, 16> multiply(const std::array<float, 16>& left, const std::array<float, 16>& right);
std::array<float, 16> makePerspective(float vertical_fov_radians, float aspect_ratio, float near_plane, float far_plane);
std::array<float, 16> makeLookAt(
    float eye_x,
    float eye_y,
    float eye_z,
    float target_x,
    float target_y,
    float target_z,
    float up_x,
    float up_y,
    float up_z
);

CameraData makeCameraData(const ViewportRenderer::CameraState& camera);
mesh::Vec3 makeRayDirection(const CameraData& camera, float normalized_x, float normalized_y, float aspect_ratio);
float worldUnitsPerPixel(float depth, float aspect_ratio, int viewport_width, int viewport_height);
float distanceToRaySquared(const mesh::Vec3& point, const mesh::Vec3& ray_origin, const mesh::Vec3& ray_direction, float* t_out);

HitCandidate findNearestTriangleHit(
    const std::vector<mesh::Vec3>& positions,
    const std::vector<mesh::Triangle>& triangles,
    const mesh::Vec3& ray_origin,
    const mesh::Vec3& ray_direction
);
HitCandidate findNearestPointHit(
    const std::vector<mesh::Vec3>& positions,
    const mesh::Vec3& ray_origin,
    const mesh::Vec3& ray_direction,
    float aspect_ratio,
    int viewport_width,
    int viewport_height
);
HitCandidate findNearestEdgeHit(
    const std::vector<mesh::Vec3>& positions,
    const std::vector<ViewportRenderer::Edge>& edges,
    const mesh::Vec3& ray_origin,
    const mesh::Vec3& ray_direction,
    float aspect_ratio,
    int viewport_width,
    int viewport_height
);

std::uint64_t edgeKey(std::uint32_t left, std::uint32_t right);
ProjectedPoint projectPointToViewport(const mesh::Vec3& point, const CameraData& camera, float aspect_ratio);
bool pointInNormalizedRect(float x, float y, float min_x, float min_y, float max_x, float max_y);
float cross2d(const Vec2& left, const Vec2& right);
bool segmentsIntersect(const Vec2& a_start, const Vec2& a_end, const Vec2& b_start, const Vec2& b_end);
bool pointInTriangle2d(const Vec2& point, const Vec2& a, const Vec2& b, const Vec2& c);
bool segmentIntersectsRect(const Vec2& start, const Vec2& end, float min_x, float min_y, float max_x, float max_y);
bool triangleIntersectsRect(const Vec2& a, const Vec2& b, const Vec2& c, float min_x, float min_y, float max_x, float max_y);

std::vector<float> readDepthBuffer(std::uint32_t framebuffer, int width, int height);
float sampleDepthBuffer(
    const std::vector<float>& depth_buffer,
    int width,
    int height,
    float normalized_x,
    float normalized_y
);
bool isProjectedPointVisible(
    const ProjectedPoint& projected_point,
    const std::vector<float>& depth_buffer,
    int width,
    int height
);

}  // namespace meshtools::render::detail
