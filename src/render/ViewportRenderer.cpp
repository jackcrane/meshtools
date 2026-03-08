#include "meshtools/render/ViewportRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
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
out vec3 v_position;
out vec3 v_normal;

void main() {
    v_position = a_position;
    v_normal = a_normal;
    gl_Position = u_mvp * vec4(a_position, 1.0);
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

}  // namespace

ViewportRenderer::ViewportRenderer() = default;

ViewportRenderer::~ViewportRenderer() {
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

void ViewportRenderer::render(const mesh::MeshDocument* document, int width, int height) {
    const int safe_width = std::max(width, 1);
    const int safe_height = std::max(height, 1);

    ensureFramebuffer(safe_width, safe_height);
    ensureShaderProgram();
    syncMesh(document);

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, safe_width, safe_height);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.14F, 0.15F, 0.17F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shader_program_);

    const float aspect_ratio = static_cast<float>(safe_width) / static_cast<float>(safe_height);
    const auto projection = makePerspective(0.85F, aspect_ratio, 0.1F, 10.0F);
    const float cos_pitch = std::cos(camera_.pitch);
    const float eye_x = camera_.target_x + (camera_.distance * cos_pitch * std::sin(camera_.yaw));
    const float eye_y = camera_.target_y + (camera_.distance * std::sin(camera_.pitch));
    const float eye_z = camera_.target_z + (camera_.distance * cos_pitch * std::cos(camera_.yaw));
    const auto view = makeLookAt(
        eye_x,
        eye_y,
        eye_z,
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
    glUniformMatrix4fv(mvp_location, 1, GL_FALSE, mvp.data());
    glUniform3f(camera_position_location, eye_x, eye_y, eye_z);

    glBindVertexArray(vertex_array_);

    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0F, 1.0F);
    glUniform1i(render_mode_location, 0);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count_), GL_UNSIGNED_INT, nullptr);

    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glUniform1i(render_mode_location, 1);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count_), GL_UNSIGNED_INT, nullptr);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_BLEND);

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

bool ViewportRenderer::UploadedMeshState::matches(const mesh::MeshDocument* document) const {
    if (document == nullptr) {
        return source_path.empty() && vertex_count == 0 && triangle_count == 0;
    }

    return source_path == document->source_path &&
           vertex_count == document->positions.size() &&
           triangle_count == document->triangles.size();
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

void ViewportRenderer::ensurePlaceholderMesh() {
    std::vector<Vertex> vertices = {
        Vertex{{-0.70F, -0.55F, 0.0F}, {0.0F, 0.0F, 1.0F}},
        Vertex{{0.70F, -0.55F, 0.0F}, {0.0F, 0.0F, 1.0F}},
        Vertex{{0.0F, 0.72F, 0.0F}, {0.0F, 0.0F, 1.0F}},
    };
    std::vector<std::uint32_t> indices = {0, 1, 2};
    uploadGeometry(vertices, indices);
    uploaded_mesh_state_ = {};
}

void ViewportRenderer::syncMesh(const mesh::MeshDocument* document) {
    if (document == nullptr) {
        if (index_count_ == 0 || !uploaded_mesh_state_.matches(nullptr)) {
            ensurePlaceholderMesh();
        }
        return;
    }

    if (uploaded_mesh_state_.matches(document)) {
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

    std::vector<mesh::Vec3> normalized_positions;
    normalized_positions.reserve(document->positions.size());
    for (const mesh::Vec3& position : document->positions) {
        normalized_positions.push_back(mesh::Vec3{
            .x = (position.x - center.x) * model_scale,
            .y = (position.y - center.y) * model_scale,
            .z = (position.z - center.z) * model_scale,
        });
    }

    std::vector<mesh::Vec3> accumulated_normals(document->positions.size(), mesh::Vec3{});
    for (const mesh::Triangle& triangle : document->triangles) {
        const mesh::Vec3 edge_ab = subtract(normalized_positions[triangle.b], normalized_positions[triangle.a]);
        const mesh::Vec3 edge_ac = subtract(normalized_positions[triangle.c], normalized_positions[triangle.a]);
        const mesh::Vec3 face_normal = normalize(cross(edge_ab, edge_ac));

        accumulated_normals[triangle.a] = add(accumulated_normals[triangle.a], face_normal);
        accumulated_normals[triangle.b] = add(accumulated_normals[triangle.b], face_normal);
        accumulated_normals[triangle.c] = add(accumulated_normals[triangle.c], face_normal);
    }

    std::vector<Vertex> vertices;
    vertices.reserve(normalized_positions.size());
    for (std::size_t index = 0; index < normalized_positions.size(); ++index) {
        const mesh::Vec3 normal = normalize(accumulated_normals[index]);
        vertices.push_back(Vertex{
            {normalized_positions[index].x, normalized_positions[index].y, normalized_positions[index].z},
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

    uploadGeometry(vertices, indices);
    uploaded_mesh_state_ = UploadedMeshState{
        .source_path = document->source_path,
        .vertex_count = document->positions.size(),
        .triangle_count = document->triangles.size(),
    };
}

void ViewportRenderer::uploadGeometry(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices) {
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

}  // namespace meshtools::render
