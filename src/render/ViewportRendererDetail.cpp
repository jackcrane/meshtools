#include "ViewportRendererDetail.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#endif

namespace meshtools::render::detail {
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
uniform vec4 u_wireframe_color;
uniform vec4 u_point_color;
uniform vec3 u_base_color;
uniform vec3 u_sky_ambient_color;
uniform vec3 u_ground_bounce_color;
uniform vec3 u_key_light_color;
uniform vec3 u_fill_light_color;
uniform vec3 u_rim_light_color;
out vec4 out_color;

void main() {
    if (u_render_mode == 1) {
        out_color = u_wireframe_color;
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

        out_color = vec4(u_point_color.rgb, alpha * u_point_color.a);
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

    vec3 base_color = u_base_color;
    vec3 sky_ambient = u_sky_ambient_color * sky;
    vec3 ground_bounce = u_ground_bounce_color * ground;
    vec3 key_light = u_key_light_color * key * 0.95;
    vec3 fill_light = u_fill_light_color * fill * 0.50;
    vec3 rim_light = u_rim_light_color * rim * 0.65;

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

}  // namespace

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
    HitCandidate best_hit{.t = std::numeric_limits<float>::infinity()};

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
    HitCandidate best_hit{.t = std::numeric_limits<float>::infinity()};

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
    HitCandidate best_hit{.t = std::numeric_limits<float>::infinity()};

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

ProjectedPoint projectPointToViewport(const mesh::Vec3& point, const CameraData& camera, float aspect_ratio) {
    const mesh::Vec3 to_point = subtract(point, camera.eye);
    const float view_x = dot(to_point, camera.right);
    const float view_y = dot(to_point, camera.up);
    const float view_z = dot(to_point, camera.forward);
    if (view_z <= kNearPlane) {
        return ProjectedPoint{};
    }

    const float tan_half_fov = std::tan(kVerticalFovRadians * 0.5F);
    const float ndc_x = view_x / (view_z * tan_half_fov * aspect_ratio);
    const float ndc_y = view_y / (view_z * tan_half_fov);
    const float ndc_z =
        ((kFarPlane + kNearPlane) / (kFarPlane - kNearPlane)) -
        ((2.0F * kFarPlane * kNearPlane) / ((kFarPlane - kNearPlane) * view_z));
    if (ndc_x < -1.2F || ndc_x > 1.2F || ndc_y < -1.2F || ndc_y > 1.2F) {
        return ProjectedPoint{};
    }

    return ProjectedPoint{
        .valid = true,
        .normalized_x = (ndc_x + 1.0F) * 0.5F,
        .normalized_y = (1.0F - ndc_y) * 0.5F,
        .view_depth = view_z,
        .depth_buffer_value = (ndc_z * 0.5F) + 0.5F,
    };
}

bool pointInNormalizedRect(float x, float y, float min_x, float min_y, float max_x, float max_y) {
    return x >= min_x && x <= max_x && y >= min_y && y <= max_y;
}

float cross2d(const Vec2& left, const Vec2& right) {
    return (left.x * right.y) - (left.y * right.x);
}

bool segmentsIntersect(const Vec2& a_start, const Vec2& a_end, const Vec2& b_start, const Vec2& b_end) {
    const Vec2 segment_a{a_end.x - a_start.x, a_end.y - a_start.y};
    const Vec2 segment_b{b_end.x - b_start.x, b_end.y - b_start.y};
    const Vec2 offset{b_start.x - a_start.x, b_start.y - a_start.y};
    const float denominator = cross2d(segment_a, segment_b);
    if (std::abs(denominator) <= kIntersectionEpsilon) {
        return false;
    }

    const float t = cross2d(offset, segment_b) / denominator;
    const float u = cross2d(offset, segment_a) / denominator;
    return t >= 0.0F && t <= 1.0F && u >= 0.0F && u <= 1.0F;
}

bool pointInTriangle2d(const Vec2& point, const Vec2& a, const Vec2& b, const Vec2& c) {
    const Vec2 ab{b.x - a.x, b.y - a.y};
    const Vec2 bc{c.x - b.x, c.y - b.y};
    const Vec2 ca{a.x - c.x, a.y - c.y};
    const Vec2 ap{point.x - a.x, point.y - a.y};
    const Vec2 bp{point.x - b.x, point.y - b.y};
    const Vec2 cp{point.x - c.x, point.y - c.y};
    const float cross_ab = cross2d(ab, ap);
    const float cross_bc = cross2d(bc, bp);
    const float cross_ca = cross2d(ca, cp);
    const bool has_negative = cross_ab < 0.0F || cross_bc < 0.0F || cross_ca < 0.0F;
    const bool has_positive = cross_ab > 0.0F || cross_bc > 0.0F || cross_ca > 0.0F;
    return !(has_negative && has_positive);
}

bool segmentIntersectsRect(const Vec2& start, const Vec2& end, float min_x, float min_y, float max_x, float max_y) {
    if (pointInNormalizedRect(start.x, start.y, min_x, min_y, max_x, max_y) ||
        pointInNormalizedRect(end.x, end.y, min_x, min_y, max_x, max_y)) {
        return true;
    }

    const std::array<std::pair<Vec2, Vec2>, 4> rect_edges = {{
        {Vec2{min_x, min_y}, Vec2{max_x, min_y}},
        {Vec2{max_x, min_y}, Vec2{max_x, max_y}},
        {Vec2{max_x, max_y}, Vec2{min_x, max_y}},
        {Vec2{min_x, max_y}, Vec2{min_x, min_y}},
    }};

    for (const auto& [edge_start, edge_end] : rect_edges) {
        if (segmentsIntersect(start, end, edge_start, edge_end)) {
            return true;
        }
    }

    return false;
}

bool triangleIntersectsRect(const Vec2& a, const Vec2& b, const Vec2& c, float min_x, float min_y, float max_x, float max_y) {
    if (pointInNormalizedRect(a.x, a.y, min_x, min_y, max_x, max_y) ||
        pointInNormalizedRect(b.x, b.y, min_x, min_y, max_x, max_y) ||
        pointInNormalizedRect(c.x, c.y, min_x, min_y, max_x, max_y)) {
        return true;
    }

    const std::array<Vec2, 4> rect_corners = {
        Vec2{min_x, min_y},
        Vec2{max_x, min_y},
        Vec2{max_x, max_y},
        Vec2{min_x, max_y},
    };
    for (const Vec2& corner : rect_corners) {
        if (pointInTriangle2d(corner, a, b, c)) {
            return true;
        }
    }

    return segmentIntersectsRect(a, b, min_x, min_y, max_x, max_y) ||
           segmentIntersectsRect(b, c, min_x, min_y, max_x, max_y) ||
           segmentIntersectsRect(c, a, min_x, min_y, max_x, max_y);
}

std::vector<float> readDepthBuffer(std::uint32_t framebuffer, int width, int height) {
    std::vector<float> depth_buffer(static_cast<std::size_t>(std::max(width, 1) * std::max(height, 1)), 1.0F);
    if (framebuffer == 0 || width <= 0 || height <= 0) {
        return depth_buffer;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, depth_buffer.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return depth_buffer;
}

float sampleDepthBuffer(
    const std::vector<float>& depth_buffer,
    int width,
    int height,
    float normalized_x,
    float normalized_y
) {
    if (depth_buffer.empty() || width <= 0 || height <= 0) {
        return 1.0F;
    }

    const int pixel_x = std::clamp(static_cast<int>(std::lround(normalized_x * static_cast<float>(width - 1))), 0, width - 1);
    const int pixel_y = std::clamp(static_cast<int>(std::lround((1.0F - normalized_y) * static_cast<float>(height - 1))), 0, height - 1);
    return depth_buffer[static_cast<std::size_t>((pixel_y * width) + pixel_x)];
}

bool isProjectedPointVisible(
    const ProjectedPoint& projected_point,
    const std::vector<float>& depth_buffer,
    int width,
    int height
) {
    if (!projected_point.valid) {
        return false;
    }

    const float sampled_depth = sampleDepthBuffer(depth_buffer, width, height, projected_point.normalized_x, projected_point.normalized_y);
    return projected_point.depth_buffer_value <= (sampled_depth + kDepthBufferVisibilityEpsilon);
}

}  // namespace meshtools::render::detail
