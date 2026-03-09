#include "meshtools/render/ViewportRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace meshtools::render {
namespace {

constexpr const char* kVertexShaderSource = R"(
#version 150 core

in vec3 a_position;
in vec3 a_normal;
uniform mat4 u_mvp;
uniform int u_render_mode;
uniform float u_point_size;
out vec3 v_position;
out vec3 v_normal;

void main() {
    vec3 position = a_position;
    if (u_render_mode == 2) {
        gl_PointSize = u_point_size;
    }

    v_position = position;
    v_normal = a_normal;
    gl_Position = u_mvp * vec4(position, 1.0);
}
)";

constexpr const char* kFragmentShaderSource = R"(
#version 150 core

in vec3 v_position;
in vec3 v_normal;
uniform vec3 u_camera_position;
uniform int u_render_mode;
out vec4 out_color;

void main() {
    if (u_render_mode == 1) {
        out_color = vec4(0.07, 0.08, 0.10, 0.92);
        return;
    }

    if (u_render_mode == 2) {
        vec2 centered = (gl_PointCoord * 2.0) - vec2(1.0);
        float arm_distance = min(abs(centered.x), abs(centered.y));
        float arm_alpha = 1.0 - smoothstep(0.10, 0.22, arm_distance);
        float radius_alpha = 1.0 - smoothstep(0.82, 1.00, length(centered));
        float alpha = arm_alpha * radius_alpha;
        if (alpha <= 0.01) {
            discard;
        }

        out_color = vec4(vec3(0.0), alpha);
        return;
    }

    vec3 normal = normalize(v_normal);
    vec3 view_dir = normalize(u_camera_position - v_position);

    vec3 key_light_dir = normalize(vec3(-0.55, 0.75, 0.45));
    vec3 fill_light_dir = normalize(vec3(0.85, 0.25, -0.35));
    vec3 rim_light_dir = normalize(vec3(-0.25, 0.35, -0.90));

    float key = max(dot(normal, key_light_dir), 0.0);
    float fill = max(dot(normal, fill_light_dir), 0.0);
    float rim = pow(1.0 - max(dot(normal, view_dir), 0.0), 2.2) * max(dot(normal, rim_light_dir), 0.0);
    float sky = clamp((normal.y * 0.5) + 0.5, 0.0, 1.0);
    float ground = clamp((-normal.y * 0.5) + 0.5, 0.0, 1.0);

    vec3 base_color = vec3(0.74, 0.78, 0.84);
    vec3 sky_ambient = vec3(0.20, 0.24, 0.30) * sky;
    vec3 ground_bounce = vec3(0.08, 0.07, 0.06) * ground;
    vec3 key_light = vec3(0.98, 0.96, 0.92) * key * 0.95;
    vec3 fill_light = vec3(0.44, 0.55, 0.76) * fill * 0.50;
    vec3 rim_light = vec3(0.96, 0.84, 0.72) * rim * 0.65;

    vec3 lit_color = (base_color * (sky_ambient + ground_bounce + vec3(0.10))) + key_light + fill_light + rim_light;
    out_color = vec4(lit_color, 1.0);
}
)";

constexpr const char* kAxisVertexShaderSource = R"(
#version 150 core

in vec3 a_position;
in vec3 a_color;
uniform mat4 u_mvp;
out vec3 v_color;

void main() {
    v_color = a_color;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)";

constexpr const char* kAxisFragmentShaderSource = R"(
#version 150 core

in vec3 v_color;
out vec4 out_color;

void main() {
    out_color = vec4(v_color, 1.0);
}
)";

constexpr const char* kHighlightVertexShaderSource = R"(
#version 150 core

in vec3 a_position;
uniform mat4 u_mvp;
uniform float u_point_size;
uniform float u_depth_bias;

void main() {
    gl_Position = u_mvp * vec4(a_position, 1.0);
    gl_Position.z -= u_depth_bias * gl_Position.w;
    gl_PointSize = u_point_size;
}
)";

constexpr const char* kHighlightFragmentShaderSource = R"(
#version 150 core

uniform vec4 u_color;
uniform int u_round_points;
out vec4 out_color;

void main() {
    if (u_round_points != 0) {
        vec2 centered = (gl_PointCoord * 2.0) - vec2(1.0);
        if (dot(centered, centered) > 1.0) {
            discard;
        }
    }

    out_color = u_color;
}
)";

constexpr float kVerticalFovRadians = 0.85F;
constexpr float kNearPlane = 0.1F;
constexpr float kFarPlane = 10.0F;
constexpr float kDefaultPointSize = 7.0F;
constexpr float kSelectionPointSize = 11.0F;
constexpr float kFaceDepthBias = 0.0012F;
constexpr float kEdgeDepthBias = 0.0018F;
constexpr float kPointDepthBias = 0.0022F;
constexpr float kEdgeHitRadiusPixels = 8.0F;
constexpr float kPointHitRadiusPixels = 11.0F;
constexpr float kSelectionDepthTolerance = 0.03F;
constexpr float kIntersectionEpsilon = 0.00001F;

struct CameraData {
    mesh::Vec3 eye;
    mesh::Vec3 right;
    mesh::Vec3 up;
    mesh::Vec3 forward;
};

struct HitCandidate {
    bool hit = false;
    std::uint32_t index = 0;
    float t = std::numeric_limits<float>::infinity();
};

mesh::Vec3 add(const mesh::Vec3& left, const mesh::Vec3& right) {
    return mesh::Vec3{
        .x = left.x + right.x,
        .y = left.y + right.y,
        .z = left.z + right.z,
    };
}

mesh::Vec3 subtract(const mesh::Vec3& left, const mesh::Vec3& right) {
    return mesh::Vec3{
        .x = left.x - right.x,
        .y = left.y - right.y,
        .z = left.z - right.z,
    };
}

mesh::Vec3 scale(const mesh::Vec3& value, float factor) {
    return mesh::Vec3{
        .x = value.x * factor,
        .y = value.y * factor,
        .z = value.z * factor,
    };
}

float length(const mesh::Vec3& value) {
    return std::sqrt((value.x * value.x) + (value.y * value.y) + (value.z * value.z));
}

float dot(const mesh::Vec3& left, const mesh::Vec3& right) {
    return (left.x * right.x) + (left.y * right.y) + (left.z * right.z);
}

mesh::Vec3 normalize(const mesh::Vec3& value) {
    const float value_length = length(value);
    if (value_length <= 0.000001F) {
        return mesh::Vec3{.x = 0.0F, .y = 1.0F, .z = 0.0F};
    }

    return scale(value, 1.0F / value_length);
}

mesh::Vec3 cross(const mesh::Vec3& left, const mesh::Vec3& right) {
    return mesh::Vec3{
        .x = (left.y * right.z) - (left.z * right.y),
        .y = (left.z * right.x) - (left.x * right.z),
        .z = (left.x * right.y) - (left.y * right.x),
    };
}

mesh::Vec3 rotateXAxisNegative90(const mesh::Vec3& value) {
    return mesh::Vec3{
        .x = value.x,
        .y = value.z,
        .z = -value.y,
    };
}

std::uint32_t compileShader(std::uint32_t shader_type, const char* source) {
    const std::uint32_t shader = glCreateShader(shader_type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        return shader;
    }

    int log_length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    std::string log(static_cast<std::size_t>(std::max(log_length, 0)), '\0');
    if (!log.empty()) {
        glGetShaderInfoLog(shader, log_length, nullptr, log.data());
    }
    glDeleteShader(shader);
    throw std::runtime_error("Failed to compile viewport shader: " + log);
}

std::uint32_t createShaderProgram() {
    const std::uint32_t vertex_shader = compileShader(GL_VERTEX_SHADER, kVertexShaderSource);
    const std::uint32_t fragment_shader = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSource);

    const std::uint32_t program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindAttribLocation(program, 0, "a_position");
    glBindAttribLocation(program, 1, "a_normal");
    glLinkProgram(program);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) {
        return program;
    }

    int log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    std::string log(static_cast<std::size_t>(std::max(log_length, 0)), '\0');
    if (!log.empty()) {
        glGetProgramInfoLog(program, log_length, nullptr, log.data());
    }
    glDeleteProgram(program);
    throw std::runtime_error("Failed to link viewport shader program: " + log);
}

std::uint32_t createAxisShaderProgram() {
    const std::uint32_t vertex_shader = compileShader(GL_VERTEX_SHADER, kAxisVertexShaderSource);
    const std::uint32_t fragment_shader = compileShader(GL_FRAGMENT_SHADER, kAxisFragmentShaderSource);

    const std::uint32_t program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindAttribLocation(program, 0, "a_position");
    glBindAttribLocation(program, 1, "a_color");
    glLinkProgram(program);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) {
        return program;
    }

    int log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    std::string log(static_cast<std::size_t>(std::max(log_length, 0)), '\0');
    if (!log.empty()) {
        glGetProgramInfoLog(program, log_length, nullptr, log.data());
    }
    glDeleteProgram(program);
    throw std::runtime_error("Failed to link axis shader program: " + log);
}

std::uint32_t createHighlightShaderProgram() {
    const std::uint32_t vertex_shader = compileShader(GL_VERTEX_SHADER, kHighlightVertexShaderSource);
    const std::uint32_t fragment_shader = compileShader(GL_FRAGMENT_SHADER, kHighlightFragmentShaderSource);

    const std::uint32_t program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glBindAttribLocation(program, 0, "a_position");
    glLinkProgram(program);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    int linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) {
        return program;
    }

    int log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    std::string log(static_cast<std::size_t>(std::max(log_length, 0)), '\0');
    if (!log.empty()) {
        glGetProgramInfoLog(program, log_length, nullptr, log.data());
    }
    glDeleteProgram(program);
    throw std::runtime_error("Failed to link highlight shader program: " + log);
}

std::array<float, 16> multiply(const std::array<float, 16>& left, const std::array<float, 16>& right) {
    std::array<float, 16> result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result[static_cast<std::size_t>((column * 4) + row)] =
                (left[static_cast<std::size_t>((0 * 4) + row)] * right[static_cast<std::size_t>((column * 4) + 0)]) +
                (left[static_cast<std::size_t>((1 * 4) + row)] * right[static_cast<std::size_t>((column * 4) + 1)]) +
                (left[static_cast<std::size_t>((2 * 4) + row)] * right[static_cast<std::size_t>((column * 4) + 2)]) +
                (left[static_cast<std::size_t>((3 * 4) + row)] * right[static_cast<std::size_t>((column * 4) + 3)]);
        }
    }
    return result;
}

std::array<float, 16> makePerspective(float vertical_fov_radians, float aspect_ratio, float near_plane, float far_plane) {
    const float tan_half_fov = std::tan(vertical_fov_radians * 0.5F);
    std::array<float, 16> matrix{};
    matrix[0] = 1.0F / (aspect_ratio * tan_half_fov);
    matrix[5] = 1.0F / tan_half_fov;
    matrix[10] = -(far_plane + near_plane) / (far_plane - near_plane);
    matrix[11] = -1.0F;
    matrix[14] = -(2.0F * far_plane * near_plane) / (far_plane - near_plane);
    return matrix;
}

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
) {
    float forward_x = target_x - eye_x;
    float forward_y = target_y - eye_y;
    float forward_z = target_z - eye_z;
    const float forward_length = std::sqrt((forward_x * forward_x) + (forward_y * forward_y) + (forward_z * forward_z));
    forward_x /= forward_length;
    forward_y /= forward_length;
    forward_z /= forward_length;

    float side_x = (forward_y * up_z) - (forward_z * up_y);
    float side_y = (forward_z * up_x) - (forward_x * up_z);
    float side_z = (forward_x * up_y) - (forward_y * up_x);
    const float side_length = std::sqrt((side_x * side_x) + (side_y * side_y) + (side_z * side_z));
    side_x /= side_length;
    side_y /= side_length;
    side_z /= side_length;

    const float corrected_up_x = (side_y * forward_z) - (side_z * forward_y);
    const float corrected_up_y = (side_z * forward_x) - (side_x * forward_z);
    const float corrected_up_z = (side_x * forward_y) - (side_y * forward_x);

    std::array<float, 16> matrix{};
    matrix[0] = side_x;
    matrix[1] = corrected_up_x;
    matrix[2] = -forward_x;
    matrix[4] = side_y;
    matrix[5] = corrected_up_y;
    matrix[6] = -forward_y;
    matrix[8] = side_z;
    matrix[9] = corrected_up_z;
    matrix[10] = -forward_z;
    matrix[15] = 1.0F;
    matrix[12] = -((side_x * eye_x) + (side_y * eye_y) + (side_z * eye_z));
    matrix[13] = -((corrected_up_x * eye_x) + (corrected_up_y * eye_y) + (corrected_up_z * eye_z));
    matrix[14] = (forward_x * eye_x) + (forward_y * eye_y) + (forward_z * eye_z);
    return matrix;
}

CameraData makeCameraData(const ViewportRenderer::CameraState& camera) {
    const float cos_pitch = std::cos(camera.pitch);
    const mesh::Vec3 eye{
        .x = camera.target_x + (camera.distance * cos_pitch * std::sin(camera.yaw)),
        .y = camera.target_y + (camera.distance * std::sin(camera.pitch)),
        .z = camera.target_z + (camera.distance * cos_pitch * std::cos(camera.yaw)),
    };
    const mesh::Vec3 target{
        .x = camera.target_x,
        .y = camera.target_y,
        .z = camera.target_z,
    };
    const mesh::Vec3 world_up{.x = 0.0F, .y = 1.0F, .z = 0.0F};
    const mesh::Vec3 forward = normalize(subtract(target, eye));
    const mesh::Vec3 right = normalize(cross(forward, world_up));
    const mesh::Vec3 up = normalize(cross(right, forward));

    return CameraData{
        .eye = eye,
        .right = right,
        .up = up,
        .forward = forward,
    };
}

mesh::Vec3 makeRayDirection(const CameraData& camera, float normalized_x, float normalized_y, float aspect_ratio) {
    const float tan_half_fov = std::tan(kVerticalFovRadians * 0.5F);
    const float ndc_x = (normalized_x * 2.0F) - 1.0F;
    const float ndc_y = 1.0F - (normalized_y * 2.0F);
    const mesh::Vec3 camera_space_direction{
        .x = ndc_x * tan_half_fov * aspect_ratio,
        .y = ndc_y * tan_half_fov,
        .z = -1.0F,
    };

    return normalize(add(
        add(scale(camera.right, camera_space_direction.x), scale(camera.up, camera_space_direction.y)),
        scale(camera.forward, -camera_space_direction.z)
    ));
}

float worldUnitsPerPixel(float depth, float aspect_ratio, int viewport_width, int viewport_height) {
    const float tan_half_fov = std::tan(kVerticalFovRadians * 0.5F);
    const float vertical_units = (2.0F * std::max(depth, kNearPlane) * tan_half_fov) /
        static_cast<float>(std::max(viewport_height, 1));
    const float horizontal_units = (2.0F * std::max(depth, kNearPlane) * tan_half_fov * aspect_ratio) /
        static_cast<float>(std::max(viewport_width, 1));
    return std::max(vertical_units, horizontal_units);
}

float distanceToRaySquared(const mesh::Vec3& point, const mesh::Vec3& ray_origin, const mesh::Vec3& ray_direction, float* t_out) {
    const mesh::Vec3 origin_to_point = subtract(point, ray_origin);
    const float t = dot(origin_to_point, ray_direction);
    if (t_out != nullptr) {
        *t_out = t;
    }

    if (t <= 0.0F) {
        return dot(origin_to_point, origin_to_point);
    }

    const mesh::Vec3 closest_point = add(ray_origin, scale(ray_direction, t));
    const mesh::Vec3 delta = subtract(point, closest_point);
    return dot(delta, delta);
}

HitCandidate findNearestTriangleHit(
    const std::vector<mesh::Vec3>& positions,
    const std::vector<mesh::Triangle>& triangles,
    const mesh::Vec3& ray_origin,
    const mesh::Vec3& ray_direction
) {
    HitCandidate best_hit;

    for (std::size_t triangle_index = 0; triangle_index < triangles.size(); ++triangle_index) {
        const mesh::Triangle& triangle = triangles[triangle_index];
        const mesh::Vec3 edge_ab = subtract(positions[triangle.b], positions[triangle.a]);
        const mesh::Vec3 edge_ac = subtract(positions[triangle.c], positions[triangle.a]);
        const mesh::Vec3 p = cross(ray_direction, edge_ac);
        const float determinant = dot(edge_ab, p);
        if (std::abs(determinant) <= kIntersectionEpsilon) {
            continue;
        }

        const float inverse_determinant = 1.0F / determinant;
        const mesh::Vec3 origin_delta = subtract(ray_origin, positions[triangle.a]);
        const float barycentric_u = dot(origin_delta, p) * inverse_determinant;
        if (barycentric_u < 0.0F || barycentric_u > 1.0F) {
            continue;
        }

        const mesh::Vec3 q = cross(origin_delta, edge_ab);
        const float barycentric_v = dot(ray_direction, q) * inverse_determinant;
        if (barycentric_v < 0.0F || (barycentric_u + barycentric_v) > 1.0F) {
            continue;
        }

        const float t = dot(edge_ac, q) * inverse_determinant;
        if (t <= kIntersectionEpsilon || t >= best_hit.t) {
            continue;
        }

        best_hit.hit = true;
        best_hit.index = static_cast<std::uint32_t>(triangle_index);
        best_hit.t = t;
    }

    return best_hit;
}

HitCandidate findNearestPointHit(
    const std::vector<mesh::Vec3>& positions,
    const mesh::Vec3& ray_origin,
    const mesh::Vec3& ray_direction,
    float aspect_ratio,
    int viewport_width,
    int viewport_height
) {
    HitCandidate best_hit;

    for (std::size_t point_index = 0; point_index < positions.size(); ++point_index) {
        float t = 0.0F;
        const float distance_squared = distanceToRaySquared(positions[point_index], ray_origin, ray_direction, &t);
        if (t <= 0.0F) {
            continue;
        }

        const float hit_radius = worldUnitsPerPixel(t, aspect_ratio, viewport_width, viewport_height) * kPointHitRadiusPixels;
        if (distance_squared > (hit_radius * hit_radius) || t >= best_hit.t) {
            continue;
        }

        best_hit.hit = true;
        best_hit.index = static_cast<std::uint32_t>(point_index);
        best_hit.t = t;
    }

    return best_hit;
}

HitCandidate findNearestEdgeHit(
    const std::vector<mesh::Vec3>& positions,
    const std::vector<ViewportRenderer::Edge>& edges,
    const mesh::Vec3& ray_origin,
    const mesh::Vec3& ray_direction,
    float aspect_ratio,
    int viewport_width,
    int viewport_height
) {
    HitCandidate best_hit;

    for (std::size_t edge_index = 0; edge_index < edges.size(); ++edge_index) {
        const ViewportRenderer::Edge& edge = edges[edge_index];
        const mesh::Vec3 segment_start = positions[edge.a];
        const mesh::Vec3 segment_delta = subtract(positions[edge.b], positions[edge.a]);
        const float segment_length_squared = dot(segment_delta, segment_delta);

        float segment_parameter = 0.0F;
        float ray_parameter = 0.0F;

        if (segment_length_squared <= kIntersectionEpsilon) {
            const float distance_squared = distanceToRaySquared(segment_start, ray_origin, ray_direction, &ray_parameter);
            if (ray_parameter <= 0.0F) {
                continue;
            }

            const float hit_radius = worldUnitsPerPixel(ray_parameter, aspect_ratio, viewport_width, viewport_height) * kEdgeHitRadiusPixels;
            if (distance_squared > (hit_radius * hit_radius) || ray_parameter >= best_hit.t) {
                continue;
            }

            best_hit.hit = true;
            best_hit.index = static_cast<std::uint32_t>(edge_index);
            best_hit.t = ray_parameter;
            continue;
        }

        const mesh::Vec3 origin_delta = subtract(ray_origin, segment_start);
        const float a = dot(ray_direction, ray_direction);
        const float b = dot(ray_direction, segment_delta);
        const float c = segment_length_squared;
        const float d = dot(ray_direction, origin_delta);
        const float e = dot(segment_delta, origin_delta);
        const float denominator = (a * c) - (b * b);

        if (std::abs(denominator) > kIntersectionEpsilon) {
            ray_parameter = ((b * e) - (c * d)) / denominator;
            segment_parameter = ((a * e) - (b * d)) / denominator;
        }

        segment_parameter = std::clamp(segment_parameter, 0.0F, 1.0F);
        const mesh::Vec3 point_on_segment = add(segment_start, scale(segment_delta, segment_parameter));
        ray_parameter = std::max(dot(subtract(point_on_segment, ray_origin), ray_direction), 0.0F);

        if (ray_parameter <= 0.0F) {
            segment_parameter = std::clamp(e / c, 0.0F, 1.0F);
            const mesh::Vec3 closest_segment_point = add(segment_start, scale(segment_delta, segment_parameter));
            ray_parameter = 0.0F;
            const mesh::Vec3 separation = subtract(closest_segment_point, ray_origin);
            const float hit_radius = worldUnitsPerPixel(kNearPlane, aspect_ratio, viewport_width, viewport_height) * kEdgeHitRadiusPixels;
            if (dot(separation, separation) > (hit_radius * hit_radius)) {
                continue;
            }
        }

        const mesh::Vec3 closest_ray_point = add(ray_origin, scale(ray_direction, ray_parameter));
        const mesh::Vec3 closest_segment_point = add(segment_start, scale(segment_delta, segment_parameter));
        const mesh::Vec3 separation = subtract(closest_segment_point, closest_ray_point);
        const float distance_squared = dot(separation, separation);
        const float hit_radius = worldUnitsPerPixel(ray_parameter, aspect_ratio, viewport_width, viewport_height) * kEdgeHitRadiusPixels;
        if (distance_squared > (hit_radius * hit_radius) || ray_parameter >= best_hit.t) {
            continue;
        }

        best_hit.hit = true;
        best_hit.index = static_cast<std::uint32_t>(edge_index);
        best_hit.t = ray_parameter;
    }

    return best_hit;
}

std::uint64_t edgeKey(std::uint32_t left, std::uint32_t right) {
    const auto [minimum, maximum] = std::minmax(left, right);
    return (static_cast<std::uint64_t>(minimum) << 32U) | static_cast<std::uint64_t>(maximum);
}

}  // namespace

ViewportRenderer::ViewportRenderer() = default;

ViewportRenderer::~ViewportRenderer() {
    if (selected_point_vertex_buffer_ != 0) {
        glDeleteBuffers(1, &selected_point_vertex_buffer_);
    }
    if (selected_point_vertex_array_ != 0) {
        glDeleteVertexArrays(1, &selected_point_vertex_array_);
    }
    if (selected_edge_vertex_buffer_ != 0) {
        glDeleteBuffers(1, &selected_edge_vertex_buffer_);
    }
    if (selected_edge_vertex_array_ != 0) {
        glDeleteVertexArrays(1, &selected_edge_vertex_array_);
    }
    if (selected_face_vertex_buffer_ != 0) {
        glDeleteBuffers(1, &selected_face_vertex_buffer_);
    }
    if (selected_face_vertex_array_ != 0) {
        glDeleteVertexArrays(1, &selected_face_vertex_array_);
    }
    if (axis_vertex_buffer_ != 0) {
        glDeleteBuffers(1, &axis_vertex_buffer_);
    }
    if (axis_vertex_array_ != 0) {
        glDeleteVertexArrays(1, &axis_vertex_array_);
    }
    if (index_buffer_ != 0) {
        glDeleteBuffers(1, &index_buffer_);
    }
    if (vertex_buffer_ != 0) {
        glDeleteBuffers(1, &vertex_buffer_);
    }
    if (vertex_array_ != 0) {
        glDeleteVertexArrays(1, &vertex_array_);
    }
    if (shader_program_ != 0) {
        glDeleteProgram(shader_program_);
    }
    if (axis_shader_program_ != 0) {
        glDeleteProgram(axis_shader_program_);
    }
    if (highlight_shader_program_ != 0) {
        glDeleteProgram(highlight_shader_program_);
    }
    if (depth_renderbuffer_ != 0) {
        glDeleteRenderbuffers(1, &depth_renderbuffer_);
    }
    if (color_texture_ != 0) {
        glDeleteTextures(1, &color_texture_);
    }
    if (framebuffer_ != 0) {
        glDeleteFramebuffers(1, &framebuffer_);
    }
}

void ViewportRenderer::render(const mesh::MeshDocument* document, int width, int height, const DisplaySettings& display_settings) {
    const int safe_width = std::max(width, 1);
    const int safe_height = std::max(height, 1);

    ensureFramebuffer(safe_width, safe_height);
    ensureShaderProgram();
    ensureAxisResources();
    ensureHighlightResources();
    syncMesh(document, display_settings.source_up_axis);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, safe_width, safe_height);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.14F, 0.15F, 0.17F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shader_program_);

    const float aspect_ratio = static_cast<float>(safe_width) / static_cast<float>(safe_height);
    const auto projection = makePerspective(kVerticalFovRadians, aspect_ratio, kNearPlane, kFarPlane);
    const CameraData camera_data = makeCameraData(camera_);
    const auto view = makeLookAt(
        camera_data.eye.x,
        camera_data.eye.y,
        camera_data.eye.z,
        camera_.target_x,
        camera_.target_y,
        camera_.target_z,
        0.0F,
        1.0F,
        0.0F
    );
    const auto mvp = multiply(projection, view);

    const int mvp_location = glGetUniformLocation(shader_program_, "u_mvp");
    const int camera_position_location = glGetUniformLocation(shader_program_, "u_camera_position");
    const int render_mode_location = glGetUniformLocation(shader_program_, "u_render_mode");
    const int point_size_location = glGetUniformLocation(shader_program_, "u_point_size");
    glUniformMatrix4fv(mvp_location, 1, GL_FALSE, mvp.data());
    glUniform3f(camera_position_location, camera_data.eye.x, camera_data.eye.y, camera_data.eye.z);
    glUniform1f(point_size_location, kDefaultPointSize);

    glBindVertexArray(vertex_array_);

    if (display_settings.shade_triangles) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0F, 1.0F);
        glUniform1i(render_mode_location, 0);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count_), GL_UNSIGNED_INT, nullptr);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    if (display_settings.show_wireframe) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glUniform1i(render_mode_location, 1);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count_), GL_UNSIGNED_INT, nullptr);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_BLEND);
    }

    if (display_settings.show_points && vertex_count_ > 0) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_PROGRAM_POINT_SIZE);
        glUniform1i(render_mode_location, 2);
        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(vertex_count_));
        glDisable(GL_PROGRAM_POINT_SIZE);
        glDisable(GL_BLEND);
    }

    if (selection_summary_.totalCount() > 0U) {
        glUseProgram(highlight_shader_program_);
        const int highlight_mvp_location = glGetUniformLocation(highlight_shader_program_, "u_mvp");
        const int highlight_color_location = glGetUniformLocation(highlight_shader_program_, "u_color");
        const int highlight_point_size_location = glGetUniformLocation(highlight_shader_program_, "u_point_size");
        const int highlight_depth_bias_location = glGetUniformLocation(highlight_shader_program_, "u_depth_bias");
        const int highlight_round_points_location = glGetUniformLocation(highlight_shader_program_, "u_round_points");

        glUniformMatrix4fv(highlight_mvp_location, 1, GL_FALSE, mvp.data());
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        if (selected_face_vertex_count_ > 0U) {
            glBindVertexArray(selected_face_vertex_array_);
            glUniform4f(highlight_color_location, 0.86F, 0.53F, 0.18F, 0.48F);
            glUniform1f(highlight_point_size_location, kSelectionPointSize);
            glUniform1f(highlight_depth_bias_location, kFaceDepthBias);
            glUniform1i(highlight_round_points_location, 0);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(selected_face_vertex_count_));
        }

        if (selected_edge_vertex_count_ > 0U) {
            glBindVertexArray(selected_edge_vertex_array_);
            glUniform4f(highlight_color_location, 1.0F, 0.52F, 0.04F, 1.0F);
            glUniform1f(highlight_point_size_location, kSelectionPointSize);
            glUniform1f(highlight_depth_bias_location, kEdgeDepthBias);
            glUniform1i(highlight_round_points_location, 0);
            glLineWidth(3.0F);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(selected_edge_vertex_count_));
        }

        if (selected_point_vertex_count_ > 0U) {
            glBindVertexArray(selected_point_vertex_array_);
            glUniform4f(highlight_color_location, 1.0F, 0.52F, 0.04F, 1.0F);
            glUniform1f(highlight_point_size_location, kSelectionPointSize);
            glUniform1f(highlight_depth_bias_location, kPointDepthBias);
            glUniform1i(highlight_round_points_location, 1);
            glEnable(GL_PROGRAM_POINT_SIZE);
            glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(selected_point_vertex_count_));
            glDisable(GL_PROGRAM_POINT_SIZE);
        }

        glDisable(GL_BLEND);
    }

    glUseProgram(axis_shader_program_);
    const int axis_mvp_location = glGetUniformLocation(axis_shader_program_, "u_mvp");
    glUniformMatrix4fv(axis_mvp_location, 1, GL_FALSE, mvp.data());
    glBindVertexArray(axis_vertex_array_);
    glLineWidth(2.0F);
    glDrawArrays(GL_LINES, 0, 6);
    glBindVertexArray(0);

    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ViewportRenderer::orbit(float delta_x, float delta_y) {
    camera_.yaw -= delta_x * 0.01F;
    camera_.pitch = std::clamp(camera_.pitch - (delta_y * 0.01F), -1.45F, 1.45F);
}

void ViewportRenderer::pan(float delta_x, float delta_y) {
    const float cos_yaw = std::cos(camera_.yaw);
    const float sin_yaw = std::sin(camera_.yaw);
    const float pan_scale = camera_.distance * 0.0015F;

    camera_.target_x += ((-cos_yaw * delta_x) + (0.0F * delta_y)) * pan_scale;
    camera_.target_y += delta_y * pan_scale;
    camera_.target_z += ((sin_yaw * delta_x) + (0.0F * delta_y)) * pan_scale;
}

void ViewportRenderer::zoom(float delta) {
    camera_.distance *= std::exp(-delta * 0.12F);
    camera_.distance = std::clamp(camera_.distance, 0.25F, 30.0F);
}

void ViewportRenderer::resetCamera() {
    camera_ = CameraState{};
}

std::size_t ViewportRenderer::selectAt(float normalized_x, float normalized_y, const SelectionQuery& selection_query) {
    if (!has_document_mesh_ ||
        normalized_positions_.empty() ||
        normalized_triangles_.empty() ||
        framebuffer_width_ <= 0 ||
        framebuffer_height_ <= 0) {
        clearSelection();
        return 0;
    }

    if (!selection_query.edges && !selection_query.faces && !selection_query.points) {
        clearSelection();
        return 0;
    }

    const float aspect_ratio = static_cast<float>(framebuffer_width_) / static_cast<float>(framebuffer_height_);
    const CameraData camera_data = makeCameraData(camera_);
    const mesh::Vec3 ray_direction = makeRayDirection(camera_data, normalized_x, normalized_y, aspect_ratio);

    const HitCandidate front_face_hit = findNearestTriangleHit(
        normalized_positions_,
        normalized_triangles_,
        camera_data.eye,
        ray_direction
    );
    HitCandidate edge_hit;
    HitCandidate point_hit;

    if (selection_query.edges) {
        edge_hit = findNearestEdgeHit(
            normalized_positions_,
            unique_edges_,
            camera_data.eye,
            ray_direction,
            aspect_ratio,
            framebuffer_width_,
            framebuffer_height_
        );
    }

    if (selection_query.points) {
        point_hit = findNearestPointHit(
            normalized_positions_,
            camera_data.eye,
            ray_direction,
            aspect_ratio,
            framebuffer_width_,
            framebuffer_height_
        );
    }

    const float occlusion_limit = front_face_hit.hit
        ? (front_face_hit.t + kSelectionDepthTolerance)
        : std::numeric_limits<float>::infinity();

    if (edge_hit.hit && edge_hit.t > occlusion_limit) {
        edge_hit = HitCandidate{};
    }
    if (point_hit.hit && point_hit.t > occlusion_limit) {
        point_hit = HitCandidate{};
    }

    const bool face_selectable = selection_query.faces && front_face_hit.hit;
    float front_t = std::numeric_limits<float>::infinity();
    if (face_selectable) {
        front_t = std::min(front_t, front_face_hit.t);
    }
    if (edge_hit.hit) {
        front_t = std::min(front_t, edge_hit.t);
    }
    if (point_hit.hit) {
        front_t = std::min(front_t, point_hit.t);
    }

    if (!std::isfinite(front_t)) {
        clearSelection();
        return 0;
    }

    selected_entities_ = SelectedEntities{};
    if (face_selectable && std::abs(front_face_hit.t - front_t) <= kSelectionDepthTolerance) {
        selected_entities_.face_index = front_face_hit.index;
    }
    if (edge_hit.hit && std::abs(edge_hit.t - front_t) <= kSelectionDepthTolerance) {
        selected_entities_.edge_index = edge_hit.index;
    }
    if (point_hit.hit && std::abs(point_hit.t - front_t) <= kSelectionDepthTolerance) {
        selected_entities_.point_index = point_hit.index;
    }

    selection_summary_ = SelectionSummary{
        .edge_count = selected_entities_.edge_index.has_value() ? 1U : 0U,
        .face_count = selected_entities_.face_index.has_value() ? 1U : 0U,
        .point_count = selected_entities_.point_index.has_value() ? 1U : 0U,
    };
    updateHighlightBuffers();
    return selection_summary_.totalCount();
}

void ViewportRenderer::clearSelection() {
    selected_entities_ = SelectedEntities{};
    selection_summary_ = SelectionSummary{};
    updateHighlightBuffers();
}

std::uint32_t ViewportRenderer::textureId() const {
    return color_texture_;
}

int ViewportRenderer::textureWidth() const {
    return framebuffer_width_;
}

int ViewportRenderer::textureHeight() const {
    return framebuffer_height_;
}

const ViewportRenderer::CameraState& ViewportRenderer::camera() const {
    return camera_;
}

const ViewportRenderer::SelectionSummary& ViewportRenderer::selectionSummary() const {
    return selection_summary_;
}

std::size_t ViewportRenderer::SelectionSummary::totalCount() const {
    return edge_count + face_count + point_count;
}

bool ViewportRenderer::UploadedMeshState::matches(const mesh::MeshDocument* document, UpAxis up_axis) const {
    if (document == nullptr) {
        return source_path.empty() && vertex_count == 0 && triangle_count == 0 && source_up_axis == up_axis;
    }

    return source_path == document->source_path &&
           vertex_count == document->positions.size() &&
           triangle_count == document->triangles.size() &&
           source_up_axis == up_axis;
}

void ViewportRenderer::ensureFramebuffer(int width, int height) {
    if (framebuffer_ == 0) {
        glGenFramebuffers(1, &framebuffer_);
        glGenTextures(1, &color_texture_);
        glGenRenderbuffers(1, &depth_renderbuffer_);
        framebuffer_width_ = 0;
        framebuffer_height_ = 0;
    }

    if (framebuffer_width_ == width && framebuffer_height_ == height) {
        return;
    }

    framebuffer_width_ = width;
    framebuffer_height_ = height;

    glBindTexture(GL_TEXTURE_2D, color_texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindRenderbuffer(GL_RENDERBUFFER, depth_renderbuffer_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_texture_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_renderbuffer_);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        throw std::runtime_error("Viewport framebuffer is incomplete.");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ViewportRenderer::ensureShaderProgram() {
    if (shader_program_ != 0) {
        return;
    }

    shader_program_ = createShaderProgram();
    glGenVertexArrays(1, &vertex_array_);
    glGenBuffers(1, &vertex_buffer_);
    glGenBuffers(1, &index_buffer_);
}

void ViewportRenderer::ensureAxisResources() {
    if (axis_shader_program_ != 0) {
        return;
    }

    axis_shader_program_ = createAxisShaderProgram();
    glGenVertexArrays(1, &axis_vertex_array_);
    glGenBuffers(1, &axis_vertex_buffer_);

    const std::array<AxisVertex, 6> axis_vertices = {
        AxisVertex{{0.0F, 0.0F, 0.0F}, {0.95F, 0.25F, 0.25F}},
        AxisVertex{{1.25F, 0.0F, 0.0F}, {0.95F, 0.25F, 0.25F}},
        AxisVertex{{0.0F, 0.0F, 0.0F}, {0.28F, 0.86F, 0.45F}},
        AxisVertex{{0.0F, 1.25F, 0.0F}, {0.28F, 0.86F, 0.45F}},
        AxisVertex{{0.0F, 0.0F, 0.0F}, {0.30F, 0.56F, 0.96F}},
        AxisVertex{{0.0F, 0.0F, 1.25F}, {0.30F, 0.56F, 0.96F}},
    };

    glBindVertexArray(axis_vertex_array_);
    glBindBuffer(GL_ARRAY_BUFFER, axis_vertex_buffer_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(axis_vertices.size() * sizeof(AxisVertex)),
        axis_vertices.data(),
        GL_STATIC_DRAW
    );

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(AxisVertex), reinterpret_cast<const void*>(offsetof(AxisVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(AxisVertex), reinterpret_cast<const void*>(offsetof(AxisVertex, color)));

    glBindVertexArray(0);
}

void ViewportRenderer::ensureHighlightResources() {
    if (highlight_shader_program_ != 0) {
        return;
    }

    highlight_shader_program_ = createHighlightShaderProgram();

    glGenVertexArrays(1, &selected_face_vertex_array_);
    glGenBuffers(1, &selected_face_vertex_buffer_);
    glBindVertexArray(selected_face_vertex_array_);
    glBindBuffer(GL_ARRAY_BUFFER, selected_face_vertex_buffer_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(HighlightVertex), reinterpret_cast<const void*>(offsetof(HighlightVertex, position)));

    glGenVertexArrays(1, &selected_edge_vertex_array_);
    glGenBuffers(1, &selected_edge_vertex_buffer_);
    glBindVertexArray(selected_edge_vertex_array_);
    glBindBuffer(GL_ARRAY_BUFFER, selected_edge_vertex_buffer_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(HighlightVertex), reinterpret_cast<const void*>(offsetof(HighlightVertex, position)));

    glGenVertexArrays(1, &selected_point_vertex_array_);
    glGenBuffers(1, &selected_point_vertex_buffer_);
    glBindVertexArray(selected_point_vertex_array_);
    glBindBuffer(GL_ARRAY_BUFFER, selected_point_vertex_buffer_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(HighlightVertex), reinterpret_cast<const void*>(offsetof(HighlightVertex, position)));

    glBindVertexArray(0);
}

void ViewportRenderer::ensurePlaceholderMesh() {
    std::vector<Vertex> vertices = {
        Vertex{{-0.70F, -0.55F, 0.0F}, {0.0F, 0.0F, 1.0F}},
        Vertex{{0.70F, -0.55F, 0.0F}, {0.0F, 0.0F, 1.0F}},
        Vertex{{0.0F, 0.72F, 0.0F}, {0.0F, 0.0F, 1.0F}},
    };
    std::vector<std::uint32_t> indices = {0, 1, 2};
    uploadGeometry(vertices, indices);
    uploaded_mesh_state_ = {};
    normalized_positions_.clear();
    normalized_triangles_.clear();
    unique_edges_.clear();
    has_document_mesh_ = false;
    clearSelection();
}

void ViewportRenderer::syncMesh(const mesh::MeshDocument* document, UpAxis up_axis) {
    if (document == nullptr) {
        if (index_count_ == 0 || !uploaded_mesh_state_.matches(nullptr, up_axis)) {
            ensurePlaceholderMesh();
        }
        return;
    }

    if (uploaded_mesh_state_.matches(document, up_axis)) {
        return;
    }

    const mesh::Vec3 minimum = document->bounds.minimum;
    const mesh::Vec3 maximum = document->bounds.maximum;
    const mesh::Vec3 center{
        .x = (minimum.x + maximum.x) * 0.5F,
        .y = (minimum.y + maximum.y) * 0.5F,
        .z = (minimum.z + maximum.z) * 0.5F,
    };

    const float extent_x = maximum.x - minimum.x;
    const float extent_y = maximum.y - minimum.y;
    const float extent_z = maximum.z - minimum.z;
    const float largest_extent = std::max({extent_x, extent_y, extent_z, 0.0001F});
    const float model_scale = 1.8F / largest_extent;

    normalized_positions_.clear();
    normalized_positions_.reserve(document->positions.size());
    for (const mesh::Vec3& position : document->positions) {
        mesh::Vec3 normalized_position{
            .x = (position.x - center.x) * model_scale,
            .y = (position.y - center.y) * model_scale,
            .z = (position.z - center.z) * model_scale,
        };
        if (up_axis == UpAxis::Z) {
            normalized_position = rotateXAxisNegative90(normalized_position);
        }
        normalized_positions_.push_back(normalized_position);
    }

    std::vector<mesh::Vec3> accumulated_normals(document->positions.size(), mesh::Vec3{});
    for (const mesh::Triangle& triangle : document->triangles) {
        const mesh::Vec3 edge_ab = subtract(normalized_positions_[triangle.b], normalized_positions_[triangle.a]);
        const mesh::Vec3 edge_ac = subtract(normalized_positions_[triangle.c], normalized_positions_[triangle.a]);
        const mesh::Vec3 face_normal = normalize(cross(edge_ab, edge_ac));

        accumulated_normals[triangle.a] = add(accumulated_normals[triangle.a], face_normal);
        accumulated_normals[triangle.b] = add(accumulated_normals[triangle.b], face_normal);
        accumulated_normals[triangle.c] = add(accumulated_normals[triangle.c], face_normal);
    }

    std::vector<Vertex> vertices;
    vertices.reserve(normalized_positions_.size());
    for (std::size_t index = 0; index < normalized_positions_.size(); ++index) {
        const mesh::Vec3 normal = normalize(accumulated_normals[index]);
        vertices.push_back(Vertex{
            {normalized_positions_[index].x, normalized_positions_[index].y, normalized_positions_[index].z},
            {normal.x, normal.y, normal.z},
        });
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(document->triangles.size() * 3ULL);
    for (const mesh::Triangle& triangle : document->triangles) {
        indices.push_back(triangle.a);
        indices.push_back(triangle.b);
        indices.push_back(triangle.c);
    }

    normalized_triangles_ = document->triangles;
    unique_edges_.clear();
    unique_edges_.reserve(document->triangles.size() * 3ULL);
    std::unordered_set<std::uint64_t> seen_edges;
    seen_edges.reserve(document->triangles.size() * 3ULL);
    for (const mesh::Triangle& triangle : document->triangles) {
        const std::array<Edge, 3> triangle_edges = {{
            Edge{triangle.a, triangle.b},
            Edge{triangle.b, triangle.c},
            Edge{triangle.c, triangle.a},
        }};

        for (const Edge& edge : triangle_edges) {
            const std::uint64_t key = edgeKey(edge.a, edge.b);
            if (seen_edges.insert(key).second) {
                unique_edges_.push_back(edge);
            }
        }
    }

    uploadGeometry(vertices, indices);
    uploaded_mesh_state_ = UploadedMeshState{
        .source_path = document->source_path,
        .vertex_count = document->positions.size(),
        .triangle_count = document->triangles.size(),
        .source_up_axis = up_axis,
    };
    has_document_mesh_ = true;
    clearSelection();
}

void ViewportRenderer::uploadGeometry(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices) {
    vertex_count_ = static_cast<std::uint32_t>(vertices.size());
    index_count_ = static_cast<std::uint32_t>(indices.size());

    glBindVertexArray(vertex_array_);

    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
        vertices.data(),
        GL_STATIC_DRAW
    );

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
        indices.data(),
        GL_STATIC_DRAW
    );

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, normal)));

    glBindVertexArray(0);
}

void ViewportRenderer::updateHighlightBuffers() {
    if (highlight_shader_program_ == 0) {
        return;
    }

    auto upload_highlight_geometry = [](std::uint32_t vertex_array, std::uint32_t vertex_buffer, const std::vector<HighlightVertex>& vertices) {
        glBindVertexArray(vertex_array);
        glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(vertices.size() * sizeof(HighlightVertex)),
            vertices.empty() ? nullptr : vertices.data(),
            GL_DYNAMIC_DRAW
        );
    };

    std::vector<HighlightVertex> face_vertices;
    if (selected_entities_.face_index.has_value() &&
        *selected_entities_.face_index < normalized_triangles_.size()) {
        const mesh::Triangle& triangle = normalized_triangles_[*selected_entities_.face_index];
        face_vertices = {
            HighlightVertex{{normalized_positions_[triangle.a].x, normalized_positions_[triangle.a].y, normalized_positions_[triangle.a].z}},
            HighlightVertex{{normalized_positions_[triangle.b].x, normalized_positions_[triangle.b].y, normalized_positions_[triangle.b].z}},
            HighlightVertex{{normalized_positions_[triangle.c].x, normalized_positions_[triangle.c].y, normalized_positions_[triangle.c].z}},
        };
    }
    selected_face_vertex_count_ = static_cast<std::uint32_t>(face_vertices.size());
    upload_highlight_geometry(selected_face_vertex_array_, selected_face_vertex_buffer_, face_vertices);

    std::vector<HighlightVertex> edge_vertices;
    if (selected_entities_.edge_index.has_value() &&
        *selected_entities_.edge_index < unique_edges_.size()) {
        const Edge& edge = unique_edges_[*selected_entities_.edge_index];
        edge_vertices = {
            HighlightVertex{{normalized_positions_[edge.a].x, normalized_positions_[edge.a].y, normalized_positions_[edge.a].z}},
            HighlightVertex{{normalized_positions_[edge.b].x, normalized_positions_[edge.b].y, normalized_positions_[edge.b].z}},
        };
    }
    selected_edge_vertex_count_ = static_cast<std::uint32_t>(edge_vertices.size());
    upload_highlight_geometry(selected_edge_vertex_array_, selected_edge_vertex_buffer_, edge_vertices);

    std::vector<HighlightVertex> point_vertices;
    if (selected_entities_.point_index.has_value() &&
        *selected_entities_.point_index < normalized_positions_.size()) {
        const mesh::Vec3& point = normalized_positions_[*selected_entities_.point_index];
        point_vertices = {
            HighlightVertex{{point.x, point.y, point.z}},
        };
    }
    selected_point_vertex_count_ = static_cast<std::uint32_t>(point_vertices.size());
    upload_highlight_geometry(selected_point_vertex_array_, selected_point_vertex_buffer_, point_vertices);

    glBindVertexArray(0);
}

}  // namespace meshtools::render
