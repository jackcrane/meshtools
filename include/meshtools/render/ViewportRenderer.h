#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "meshtools/mesh/MeshDocument.h"

namespace meshtools::render {

class ViewportRenderer {
  public:
    struct CameraState {
        float yaw = 0.65F;
        float pitch = 0.45F;
        float distance = 2.4F;
        float target_x = 0.0F;
        float target_y = 0.0F;
        float target_z = 0.0F;
    };

    ViewportRenderer();
    ~ViewportRenderer();

    ViewportRenderer(const ViewportRenderer&) = delete;
    ViewportRenderer& operator=(const ViewportRenderer&) = delete;

    void render(const mesh::MeshDocument* document, int width, int height);
    void orbit(float delta_x, float delta_y);
    void pan(float delta_x, float delta_y);
    void zoom(float delta);
    void resetCamera();

    [[nodiscard]] std::uint32_t textureId() const;
    [[nodiscard]] int textureWidth() const;
    [[nodiscard]] int textureHeight() const;
    [[nodiscard]] const CameraState& camera() const;

  private:
    struct UploadedMeshState {
        std::filesystem::path source_path;
        std::size_t vertex_count = 0;
        std::size_t triangle_count = 0;

        [[nodiscard]] bool matches(const mesh::MeshDocument* document) const;
    };

    struct Vertex {
        float position[3];
        float normal[3];
    };

    void ensureFramebuffer(int width, int height);
    void ensureShaderProgram();
    void ensurePlaceholderMesh();
    void syncMesh(const mesh::MeshDocument* document);
    void uploadGeometry(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices);

    std::uint32_t framebuffer_ = 0;
    std::uint32_t color_texture_ = 0;
    std::uint32_t depth_renderbuffer_ = 0;
    std::uint32_t shader_program_ = 0;
    std::uint32_t vertex_array_ = 0;
    std::uint32_t vertex_buffer_ = 0;
    std::uint32_t index_buffer_ = 0;
    std::uint32_t index_count_ = 0;
    int framebuffer_width_ = 0;
    int framebuffer_height_ = 0;
    CameraState camera_{};
    UploadedMeshState uploaded_mesh_state_{};
};

}  // namespace meshtools::render
