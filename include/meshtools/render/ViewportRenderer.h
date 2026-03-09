#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "meshtools/mesh/MeshDocument.h"

namespace meshtools::render {

class ViewportRenderer {
  public:
    enum class SelectionMode {
        Replace,
        Toggle,
        Path,
    };

    struct SelectionQuery {
        bool edges = false;
        bool faces = false;
        bool points = false;
    };

    struct SelectionSummary {
        std::size_t edge_count = 0;
        std::size_t face_count = 0;
        std::size_t point_count = 0;

        [[nodiscard]] std::size_t totalCount() const;
    };

    enum class UpAxis {
        Y,
        Z,
    };

    struct DisplaySettings {
        bool show_wireframe = true;
        bool shade_triangles = true;
        bool show_points = false;
        UpAxis source_up_axis = UpAxis::Y;
    };

    struct CameraState {
        float yaw = 0.65F;
        float pitch = 0.45F;
        float distance = 2.4F;
        float target_x = 0.0F;
        float target_y = 0.0F;
        float target_z = 0.0F;
    };

    struct Edge {
        std::uint32_t a = 0;
        std::uint32_t b = 0;
    };

    ViewportRenderer();
    ~ViewportRenderer();

    ViewportRenderer(const ViewportRenderer&) = delete;
    ViewportRenderer& operator=(const ViewportRenderer&) = delete;

    void render(const mesh::MeshDocument* document, int width, int height, const DisplaySettings& display_settings);
    void orbit(float delta_x, float delta_y);
    void pan(float delta_x, float delta_y);
    void zoom(float delta);
    void resetCamera();
    [[nodiscard]] std::size_t selectAt(
        float normalized_x,
        float normalized_y,
        const SelectionQuery& selection_query,
        SelectionMode selection_mode = SelectionMode::Replace
    );
    [[nodiscard]] std::size_t selectInRect(
        float normalized_min_x,
        float normalized_min_y,
        float normalized_max_x,
        float normalized_max_y,
        const SelectionQuery& selection_query,
        SelectionMode selection_mode = SelectionMode::Replace
    );
    [[nodiscard]] std::size_t invertSelection(const SelectionQuery& selection_query);
    void clearSelection();

    [[nodiscard]] std::uint32_t textureId() const;
    [[nodiscard]] int textureWidth() const;
    [[nodiscard]] int textureHeight() const;
    [[nodiscard]] const CameraState& camera() const;
    [[nodiscard]] const SelectionSummary& selectionSummary() const;

  private:
    struct UploadedMeshState {
        std::filesystem::path source_path;
        std::size_t vertex_count = 0;
        std::size_t triangle_count = 0;
        UpAxis source_up_axis = UpAxis::Y;

        [[nodiscard]] bool matches(const mesh::MeshDocument* document, UpAxis up_axis) const;
    };

    struct Vertex {
        float position[3];
        float normal[3];
    };

    struct AxisVertex {
        float position[3];
        float color[3];
    };

    struct HighlightVertex {
        float position[3];
    };

    void ensureFramebuffer(int width, int height);
    void ensureShaderProgram();
    void ensureAxisResources();
    void ensureHighlightResources();
    void ensurePlaceholderMesh();
    void syncMesh(const mesh::MeshDocument* document, UpAxis up_axis);
    void uploadGeometry(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices);
    void updateHighlightBuffers();

    std::uint32_t framebuffer_ = 0;
    std::uint32_t color_texture_ = 0;
    std::uint32_t depth_renderbuffer_ = 0;
    std::uint32_t shader_program_ = 0;
    std::uint32_t axis_shader_program_ = 0;
    std::uint32_t highlight_shader_program_ = 0;
    std::uint32_t vertex_array_ = 0;
    std::uint32_t vertex_buffer_ = 0;
    std::uint32_t index_buffer_ = 0;
    std::uint32_t axis_vertex_array_ = 0;
    std::uint32_t axis_vertex_buffer_ = 0;
    std::uint32_t selected_face_vertex_array_ = 0;
    std::uint32_t selected_face_vertex_buffer_ = 0;
    std::uint32_t selected_edge_vertex_array_ = 0;
    std::uint32_t selected_edge_vertex_buffer_ = 0;
    std::uint32_t selected_point_vertex_array_ = 0;
    std::uint32_t selected_point_vertex_buffer_ = 0;
    std::uint32_t vertex_count_ = 0;
    std::uint32_t index_count_ = 0;
    std::uint32_t selected_face_vertex_count_ = 0;
    std::uint32_t selected_edge_vertex_count_ = 0;
    std::uint32_t selected_point_vertex_count_ = 0;
    int framebuffer_width_ = 0;
    int framebuffer_height_ = 0;
    CameraState camera_{};
    UploadedMeshState uploaded_mesh_state_{};
    std::vector<mesh::Vec3> normalized_positions_;
    std::vector<mesh::Triangle> normalized_triangles_;
    std::vector<Edge> unique_edges_;
    std::vector<Edge> unique_edge_topology_vertices_;
    std::vector<std::vector<std::uint32_t>> face_neighbors_;
    std::vector<std::vector<std::uint32_t>> edge_neighbors_;
    std::vector<std::uint32_t> selected_edge_indices_;
    std::vector<std::uint32_t> selected_face_indices_;
    std::vector<std::uint32_t> selected_point_indices_;
    std::optional<std::uint32_t> face_selection_anchor_;
    std::optional<std::uint32_t> edge_selection_anchor_;
    SelectionSummary selection_summary_{};
    bool has_document_mesh_ = false;
};

}  // namespace meshtools::render
