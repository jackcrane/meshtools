#include "meshtools/render/ViewportRenderer.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "ViewportRendererDetail.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#endif

namespace meshtools::render {

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

    shader_program_ = detail::createShaderProgram();
    glGenVertexArrays(1, &vertex_array_);
    glGenBuffers(1, &vertex_buffer_);
    glGenBuffers(1, &index_buffer_);
}

void ViewportRenderer::ensureAxisResources() {
    if (axis_shader_program_ != 0) {
        return;
    }

    axis_shader_program_ = detail::createAxisShaderProgram();
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

    highlight_shader_program_ = detail::createHighlightShaderProgram();

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
            normalized_position = detail::rotateXAxisNegative90(normalized_position);
        }
        normalized_positions_.push_back(normalized_position);
    }

    std::vector<mesh::Vec3> accumulated_normals(document->positions.size(), mesh::Vec3{});
    for (const mesh::Triangle& triangle : document->triangles) {
        const mesh::Vec3 edge_ab = detail::subtract(normalized_positions_[triangle.b], normalized_positions_[triangle.a]);
        const mesh::Vec3 edge_ac = detail::subtract(normalized_positions_[triangle.c], normalized_positions_[triangle.a]);
        const mesh::Vec3 face_normal = detail::normalize(detail::cross(edge_ab, edge_ac));

        accumulated_normals[triangle.a] = detail::add(accumulated_normals[triangle.a], face_normal);
        accumulated_normals[triangle.b] = detail::add(accumulated_normals[triangle.b], face_normal);
        accumulated_normals[triangle.c] = detail::add(accumulated_normals[triangle.c], face_normal);
    }

    std::vector<Vertex> vertices;
    vertices.reserve(normalized_positions_.size());
    for (std::size_t index = 0; index < normalized_positions_.size(); ++index) {
        const mesh::Vec3 normal = detail::normalize(accumulated_normals[index]);
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
            const std::uint64_t key = detail::edgeKey(edge.a, edge.b);
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

}  // namespace meshtools::render
