#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "meshtools/mesh/MeshDocument.h"
#include "meshtools/render/SelectSimilar.h"

namespace meshtools::render {

class ViewportRenderer {
  public:
    struct Color {
        float r = 0.0F;
        float g = 0.0F;
        float b = 0.0F;
        float a = 1.0F;
    };

    struct ThemeColors {
        Color clear_color{0.14F, 0.15F, 0.17F, 1.0F};
        Color mesh_base_color{0.74F, 0.78F, 0.84F, 1.0F};
        Color mesh_sky_color{0.20F, 0.24F, 0.30F, 1.0F};
        Color mesh_ground_color{0.08F, 0.07F, 0.06F, 1.0F};
        Color mesh_key_light_color{0.98F, 0.96F, 0.92F, 1.0F};
        Color mesh_fill_light_color{0.44F, 0.55F, 0.76F, 1.0F};
        Color mesh_rim_light_color{0.96F, 0.84F, 0.72F, 1.0F};
        Color wireframe_color{0.07F, 0.08F, 0.10F, 0.92F};
        Color point_color{0.00F, 0.00F, 0.00F, 1.0F};
        Color document_edge_color{0.03F, 0.03F, 0.03F, 1.0F};
        Color selected_face_color{0.93F, 0.59F, 0.18F, 0.56F};
        Color preview_face_color{0.18F, 0.54F, 0.95F, 0.42F};
        Color preview_edge_color{0.22F, 0.68F, 1.0F, 1.0F};
        Color selected_edge_color{1.0F, 0.52F, 0.04F, 1.0F};
        Color selected_point_color{1.0F, 0.52F, 0.04F, 1.0F};
    };

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
        ThemeColors theme_colors{};
    };

    enum class ExpandSelectionMethod {
        Coplanar,
        Adjacent,
        IntersectingNormals,
    };

    struct ExpandSelectionParams {
        ExpandSelectionMethod method = ExpandSelectionMethod::Coplanar;
        bool coplanar_include_parallel = false;
        bool coplanar_select_adjacent_only = true;
        float coplanar_tolerance_percent = 0.01F;
        float adjacent_max_angle_degrees = 10.0F;
        bool intersecting_include_inverse_normals = false;
        float intersecting_tolerance = 0.05F;
        bool intersecting_allow_linear_intersection = false;
    };

    struct ExpandSelectionResult {
        bool available = false;
        bool linear_intersection_enabled = false;
        mesh::EntitySelection selection;
        std::vector<std::uint32_t> preview_face_indices;
        std::vector<std::string> unavailable_reasons;
    };

    struct EdgeLoopSelectionResult {
        bool available = false;
        std::size_t candidate_count = 0;
        std::size_t selected_candidate_index = 0;
        std::string candidate_label;
        std::string unavailable_reason;
        mesh::EntitySelection selection;
    };

    struct CameraState {
        float yaw = 0.65F;
        float pitch = 0.45F;
        float distance = 2.4F;
        float target_x = 0.0F;
        float target_y = 0.0F;
        float target_z = 0.0F;
    };

    struct FrameTiming {
        float total_render_ms = 0.0F;
        float framebuffer_setup_ms = 0.0F;
        float mesh_sync_ms = 0.0F;
        float shaded_pass_ms = 0.0F;
        float wireframe_pass_ms = 0.0F;
        float point_pass_ms = 0.0F;
        float highlight_pass_ms = 0.0F;
        float axis_pass_ms = 0.0F;
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
    [[nodiscard]] mesh::EntitySelection currentSelection() const;
    void setSelection(mesh::EntitySelection selection);
    [[nodiscard]] EdgeLoopSelectionResult selectEdgeLoop();
    [[nodiscard]] SelectSimilarResult evaluateSelectSimilar(const SelectSimilarParams& params) const;
    [[nodiscard]] ExpandSelectionResult evaluateExpandSelection(const ExpandSelectionParams& params) const;
    void setExpandSelectionPreview(std::vector<std::uint32_t> face_indices);
    void clearExpandSelectionPreview();
    void setSelectSimilarPreview(std::vector<std::uint32_t> edge_indices);
    void clearSelectSimilarPreview();

    [[nodiscard]] std::uint32_t textureId() const;
    [[nodiscard]] int textureWidth() const;
    [[nodiscard]] int textureHeight() const;
    [[nodiscard]] const CameraState& camera() const;
    [[nodiscard]] const FrameTiming& lastFrameTiming() const;
    [[nodiscard]] const SelectionSummary& selectionSummary() const;

  private:
    struct UploadedMeshState {
        std::filesystem::path source_path;
        std::uint64_t mesh_revision = 0;
        std::size_t vertex_count = 0;
        std::size_t triangle_count = 0;
        std::size_t explicit_edge_count = 0;
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
    std::uint32_t preview_face_vertex_array_ = 0;
    std::uint32_t preview_face_vertex_buffer_ = 0;
    std::uint32_t preview_edge_vertex_array_ = 0;
    std::uint32_t preview_edge_vertex_buffer_ = 0;
    std::uint32_t document_edge_vertex_array_ = 0;
    std::uint32_t document_edge_vertex_buffer_ = 0;
    std::uint32_t selected_edge_vertex_array_ = 0;
    std::uint32_t selected_edge_vertex_buffer_ = 0;
    std::uint32_t selected_point_vertex_array_ = 0;
    std::uint32_t selected_point_vertex_buffer_ = 0;
    std::uint32_t vertex_count_ = 0;
    std::uint32_t index_count_ = 0;
    std::uint32_t selected_face_vertex_count_ = 0;
    std::uint32_t preview_face_vertex_count_ = 0;
    std::uint32_t preview_edge_vertex_count_ = 0;
    std::uint32_t document_edge_vertex_count_ = 0;
    std::uint32_t selected_edge_vertex_count_ = 0;
    std::uint32_t selected_point_vertex_count_ = 0;
    int framebuffer_width_ = 0;
    int framebuffer_height_ = 0;
    CameraState camera_{};
    UploadedMeshState uploaded_mesh_state_{};
    std::vector<mesh::Vec3> normalized_positions_;
    std::vector<mesh::Vec3> topology_positions_;
    std::vector<mesh::Triangle> normalized_triangles_;
    std::vector<Edge> unique_edges_;
    std::vector<Edge> unique_edge_topology_vertices_;
    std::vector<std::vector<std::uint32_t>> face_neighbors_;
    std::vector<std::vector<std::uint32_t>> edge_neighbors_;
    std::vector<std::vector<std::uint32_t>> edge_face_indices_;
    std::vector<std::uint32_t> selected_edge_indices_;
    std::vector<std::uint32_t> selected_face_indices_;
    std::vector<std::uint32_t> selected_point_indices_;
    std::vector<std::uint32_t> preview_face_indices_;
    std::vector<std::uint32_t> preview_edge_indices_;
    std::optional<std::uint32_t> face_selection_anchor_;
    std::optional<std::uint32_t> edge_selection_anchor_;
    struct EdgeLoopCycleState {
        std::optional<std::uint32_t> seed_edge_index;
        std::vector<std::vector<std::uint32_t>> candidates;
        std::vector<std::string> labels;
        std::size_t selected_candidate_index = 0;
    } edge_loop_cycle_;
    SelectionSummary selection_summary_{};
    FrameTiming last_frame_timing_{};
    bool has_document_mesh_ = false;
};

}  // namespace meshtools::render
