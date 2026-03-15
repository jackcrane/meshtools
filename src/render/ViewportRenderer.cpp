#include "meshtools/render/ViewportRenderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include "ViewportRendererDetail.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#endif

namespace meshtools::render {
namespace {

template <typename Func>
float measureMilliseconds(Func&& func) {
    const auto start = std::chrono::steady_clock::now();
    std::forward<Func>(func)();
    return std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

std::vector<ViewportRenderer::HighlightVertex> ViewportRenderer::buildSelectedEdgeRibbonVertices(
    const detail::CameraData& camera_data,
    float aspect_ratio,
    int viewport_width,
    int viewport_height,
    float stroke_width
) const {
    constexpr float kMinimumEdgeLength = 0.000001F;
    const float clamped_stroke_width = std::clamp(stroke_width, 1.0F, 16.0F);

    std::vector<HighlightVertex> vertices;
    vertices.reserve(selected_edge_indices_.size() * 6ULL);

    for (const std::uint32_t edge_index : selected_edge_indices_) {
        if (edge_index >= unique_edges_.size()) {
            continue;
        }

        const Edge& edge = unique_edges_[edge_index];
        if (edge.a >= normalized_positions_.size() || edge.b >= normalized_positions_.size()) {
            continue;
        }

        const mesh::Vec3& a = normalized_positions_[edge.a];
        const mesh::Vec3& b = normalized_positions_[edge.b];
        const mesh::Vec3 edge_direction = detail::subtract(b, a);
        if (detail::length(edge_direction) <= kMinimumEdgeLength) {
            continue;
        }

        mesh::Vec3 ribbon_offset_direction = detail::cross(camera_data.forward, edge_direction);
        if (detail::length(ribbon_offset_direction) <= kMinimumEdgeLength) {
            ribbon_offset_direction = detail::cross(camera_data.up, edge_direction);
        }
        if (detail::length(ribbon_offset_direction) <= kMinimumEdgeLength) {
            ribbon_offset_direction = camera_data.right;
        } else {
            ribbon_offset_direction = detail::normalize(ribbon_offset_direction);
        }

        const mesh::Vec3 midpoint = detail::scale(detail::add(a, b), 0.5F);
        const float midpoint_depth = detail::dot(detail::subtract(midpoint, camera_data.eye), camera_data.forward);
        const float half_width =
            detail::worldUnitsPerPixel(midpoint_depth, aspect_ratio, viewport_width, viewport_height) *
            clamped_stroke_width * 0.5F;
        const mesh::Vec3 offset = detail::scale(ribbon_offset_direction, half_width);

        const mesh::Vec3 a_plus = detail::add(a, offset);
        const mesh::Vec3 a_minus = detail::subtract(a, offset);
        const mesh::Vec3 b_plus = detail::add(b, offset);
        const mesh::Vec3 b_minus = detail::subtract(b, offset);

        vertices.push_back(ViewportRenderer::HighlightVertex{{a_plus.x, a_plus.y, a_plus.z}});
        vertices.push_back(ViewportRenderer::HighlightVertex{{b_plus.x, b_plus.y, b_plus.z}});
        vertices.push_back(ViewportRenderer::HighlightVertex{{b_minus.x, b_minus.y, b_minus.z}});
        vertices.push_back(ViewportRenderer::HighlightVertex{{a_plus.x, a_plus.y, a_plus.z}});
        vertices.push_back(ViewportRenderer::HighlightVertex{{b_minus.x, b_minus.y, b_minus.z}});
        vertices.push_back(ViewportRenderer::HighlightVertex{{a_minus.x, a_minus.y, a_minus.z}});
    }

    return vertices;
}

ViewportRenderer::ViewportRenderer() = default;

ViewportRenderer::~ViewportRenderer() {
    if (preview_face_vertex_buffer_ != 0) {
        glDeleteBuffers(1, &preview_face_vertex_buffer_);
    }
    if (preview_face_vertex_array_ != 0) {
        glDeleteVertexArrays(1, &preview_face_vertex_array_);
    }
    if (preview_edge_vertex_buffer_ != 0) {
        glDeleteBuffers(1, &preview_edge_vertex_buffer_);
    }
    if (preview_edge_vertex_array_ != 0) {
        glDeleteVertexArrays(1, &preview_edge_vertex_array_);
    }
    if (document_edge_vertex_buffer_ != 0) {
        glDeleteBuffers(1, &document_edge_vertex_buffer_);
    }
    if (document_edge_vertex_array_ != 0) {
        glDeleteVertexArrays(1, &document_edge_vertex_array_);
    }
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
    const auto frame_start = std::chrono::steady_clock::now();
    FrameTiming frame_timing;
    const int safe_width = std::max(width, 1);
    const int safe_height = std::max(height, 1);

    frame_timing.framebuffer_setup_ms = measureMilliseconds([&]() {
        ensureFramebuffer(safe_width, safe_height);
    });
    ensureShaderProgram();
    ensureAxisResources();
    ensureHighlightResources();
    frame_timing.mesh_sync_ms = measureMilliseconds([&]() {
        syncMesh(document, display_settings.source_up_axis);
    });

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, safe_width, safe_height);
    glEnable(GL_DEPTH_TEST);
    glClearColor(
        display_settings.theme_colors.clear_color.r,
        display_settings.theme_colors.clear_color.g,
        display_settings.theme_colors.clear_color.b,
        display_settings.theme_colors.clear_color.a
    );
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shader_program_);

    const float aspect_ratio = static_cast<float>(safe_width) / static_cast<float>(safe_height);
    const auto projection = detail::makePerspective(detail::kVerticalFovRadians, aspect_ratio, detail::kNearPlane, detail::kFarPlane);
    const detail::CameraData camera_data = detail::makeCameraData(camera_);
    const auto view = detail::makeLookAt(
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
    const auto mvp = detail::multiply(projection, view);

    if (!selected_edge_indices_.empty()) {
        const std::vector<HighlightVertex> selected_edge_vertices = buildSelectedEdgeRibbonVertices(
            camera_data,
            aspect_ratio,
            safe_width,
            safe_height,
            display_settings.selected_edge_stroke
        );
        glBindVertexArray(selected_edge_vertex_array_);
        glBindBuffer(GL_ARRAY_BUFFER, selected_edge_vertex_buffer_);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(selected_edge_vertices.size() * sizeof(HighlightVertex)),
            selected_edge_vertices.empty() ? nullptr : selected_edge_vertices.data(),
            GL_DYNAMIC_DRAW
        );
        selected_edge_vertex_count_ = static_cast<std::uint32_t>(selected_edge_vertices.size());
    } else {
        selected_edge_vertex_count_ = 0U;
    }

    const int mvp_location = glGetUniformLocation(shader_program_, "u_mvp");
    const int camera_position_location = glGetUniformLocation(shader_program_, "u_camera_position");
    const int render_mode_location = glGetUniformLocation(shader_program_, "u_render_mode");
    const int point_size_location = glGetUniformLocation(shader_program_, "u_point_size");
    const int wireframe_color_location = glGetUniformLocation(shader_program_, "u_wireframe_color");
    const int point_color_location = glGetUniformLocation(shader_program_, "u_point_color");
    const int base_color_location = glGetUniformLocation(shader_program_, "u_base_color");
    const int sky_ambient_color_location = glGetUniformLocation(shader_program_, "u_sky_ambient_color");
    const int ground_bounce_color_location = glGetUniformLocation(shader_program_, "u_ground_bounce_color");
    const int key_light_color_location = glGetUniformLocation(shader_program_, "u_key_light_color");
    const int fill_light_color_location = glGetUniformLocation(shader_program_, "u_fill_light_color");
    const int rim_light_color_location = glGetUniformLocation(shader_program_, "u_rim_light_color");
    glUniformMatrix4fv(mvp_location, 1, GL_FALSE, mvp.data());
    glUniform3f(camera_position_location, camera_data.eye.x, camera_data.eye.y, camera_data.eye.z);
    glUniform1f(point_size_location, detail::kDefaultPointSize);
    glUniform4f(
        wireframe_color_location,
        display_settings.theme_colors.wireframe_color.r,
        display_settings.theme_colors.wireframe_color.g,
        display_settings.theme_colors.wireframe_color.b,
        display_settings.theme_colors.wireframe_color.a
    );
    glUniform4f(
        point_color_location,
        display_settings.theme_colors.point_color.r,
        display_settings.theme_colors.point_color.g,
        display_settings.theme_colors.point_color.b,
        display_settings.theme_colors.point_color.a
    );
    glUniform3f(
        base_color_location,
        display_settings.theme_colors.mesh_base_color.r,
        display_settings.theme_colors.mesh_base_color.g,
        display_settings.theme_colors.mesh_base_color.b
    );
    glUniform3f(
        sky_ambient_color_location,
        display_settings.theme_colors.mesh_sky_color.r,
        display_settings.theme_colors.mesh_sky_color.g,
        display_settings.theme_colors.mesh_sky_color.b
    );
    glUniform3f(
        ground_bounce_color_location,
        display_settings.theme_colors.mesh_ground_color.r,
        display_settings.theme_colors.mesh_ground_color.g,
        display_settings.theme_colors.mesh_ground_color.b
    );
    glUniform3f(
        key_light_color_location,
        display_settings.theme_colors.mesh_key_light_color.r,
        display_settings.theme_colors.mesh_key_light_color.g,
        display_settings.theme_colors.mesh_key_light_color.b
    );
    glUniform3f(
        fill_light_color_location,
        display_settings.theme_colors.mesh_fill_light_color.r,
        display_settings.theme_colors.mesh_fill_light_color.g,
        display_settings.theme_colors.mesh_fill_light_color.b
    );
    glUniform3f(
        rim_light_color_location,
        display_settings.theme_colors.mesh_rim_light_color.r,
        display_settings.theme_colors.mesh_rim_light_color.g,
        display_settings.theme_colors.mesh_rim_light_color.b
    );

    glBindVertexArray(vertex_array_);

    if (display_settings.shade_triangles) {
        frame_timing.shaded_pass_ms = measureMilliseconds([&]() {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0F, 1.0F);
            glUniform1i(render_mode_location, 0);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count_), GL_UNSIGNED_INT, nullptr);
            glDisable(GL_POLYGON_OFFSET_FILL);
        });
    }

    if (display_settings.show_wireframe) {
        frame_timing.wireframe_pass_ms = measureMilliseconds([&]() {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glUniform1i(render_mode_location, 1);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(index_count_), GL_UNSIGNED_INT, nullptr);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            glDisable(GL_BLEND);
        });
    }

    if (display_settings.show_points && vertex_count_ > 0) {
        frame_timing.point_pass_ms = measureMilliseconds([&]() {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_PROGRAM_POINT_SIZE);
            glUniform1i(render_mode_location, 2);
            glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(vertex_count_));
            glDisable(GL_PROGRAM_POINT_SIZE);
            glDisable(GL_BLEND);
        });
    }

    frame_timing.highlight_pass_ms = measureMilliseconds([&]() {
        glUseProgram(highlight_shader_program_);
        const int highlight_mvp_location = glGetUniformLocation(highlight_shader_program_, "u_mvp");
        const int highlight_color_location = glGetUniformLocation(highlight_shader_program_, "u_color");
        const int highlight_point_size_location = glGetUniformLocation(highlight_shader_program_, "u_point_size");
        const int highlight_depth_bias_location = glGetUniformLocation(highlight_shader_program_, "u_depth_bias");
        const int highlight_round_points_location = glGetUniformLocation(highlight_shader_program_, "u_round_points");

        glUniformMatrix4fv(highlight_mvp_location, 1, GL_FALSE, mvp.data());
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        if (document_edge_vertex_count_ > 0U) {
            glBindVertexArray(document_edge_vertex_array_);
            glUniform4f(
                highlight_color_location,
                display_settings.theme_colors.document_edge_color.r,
                display_settings.theme_colors.document_edge_color.g,
                display_settings.theme_colors.document_edge_color.b,
                display_settings.theme_colors.document_edge_color.a
            );
            glUniform1f(highlight_point_size_location, detail::kSelectionPointSize);
            glUniform1f(highlight_depth_bias_location, detail::kEdgeDepthBias * 0.75F);
            glUniform1i(highlight_round_points_location, 0);
            glLineWidth(1.5F);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(document_edge_vertex_count_));
        }

        if (selection_summary_.totalCount() > 0U) {
            if (selected_face_vertex_count_ > 0U) {
                glBindVertexArray(selected_face_vertex_array_);
                glUniform4f(
                    highlight_color_location,
                    display_settings.theme_colors.selected_face_color.r,
                    display_settings.theme_colors.selected_face_color.g,
                    display_settings.theme_colors.selected_face_color.b,
                    display_settings.theme_colors.selected_face_color.a
                );
                glUniform1f(highlight_point_size_location, detail::kSelectionPointSize);
                glUniform1f(highlight_depth_bias_location, detail::kFaceDepthBias);
                glUniform1i(highlight_round_points_location, 0);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(selected_face_vertex_count_));
            }

            if (preview_face_vertex_count_ > 0U) {
                glBindVertexArray(preview_face_vertex_array_);
                glUniform4f(
                    highlight_color_location,
                    display_settings.theme_colors.preview_face_color.r,
                    display_settings.theme_colors.preview_face_color.g,
                    display_settings.theme_colors.preview_face_color.b,
                    display_settings.theme_colors.preview_face_color.a
                );
                glUniform1f(highlight_point_size_location, detail::kSelectionPointSize);
                glUniform1f(highlight_depth_bias_location, detail::kFaceDepthBias * 0.5F);
                glUniform1i(highlight_round_points_location, 0);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(preview_face_vertex_count_));
            }

            if (preview_edge_vertex_count_ > 0U) {
                glBindVertexArray(preview_edge_vertex_array_);
                glUniform4f(
                    highlight_color_location,
                    display_settings.theme_colors.preview_edge_color.r,
                    display_settings.theme_colors.preview_edge_color.g,
                    display_settings.theme_colors.preview_edge_color.b,
                    display_settings.theme_colors.preview_edge_color.a
                );
                glUniform1f(highlight_point_size_location, detail::kSelectionPointSize);
                glUniform1f(highlight_depth_bias_location, detail::kEdgeDepthBias * 0.85F);
                glUniform1i(highlight_round_points_location, 0);
                glLineWidth(3.0F);
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(preview_edge_vertex_count_));
            }

            if (selected_edge_vertex_count_ > 0U) {
                glBindVertexArray(selected_edge_vertex_array_);
                glUniform4f(
                    highlight_color_location,
                    display_settings.theme_colors.selected_edge_color.r,
                    display_settings.theme_colors.selected_edge_color.g,
                    display_settings.theme_colors.selected_edge_color.b,
                    display_settings.theme_colors.selected_edge_color.a
                );
                glUniform1f(highlight_point_size_location, detail::kSelectionPointSize);
                glUniform1f(highlight_depth_bias_location, detail::kEdgeDepthBias);
                glUniform1i(highlight_round_points_location, 0);
                glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(selected_edge_vertex_count_));
            }

            if (selected_point_vertex_count_ > 0U) {
                glBindVertexArray(selected_point_vertex_array_);
                glUniform4f(
                    highlight_color_location,
                    display_settings.theme_colors.selected_point_color.r,
                    display_settings.theme_colors.selected_point_color.g,
                    display_settings.theme_colors.selected_point_color.b,
                    display_settings.theme_colors.selected_point_color.a
                );
                glUniform1f(highlight_point_size_location, detail::kSelectionPointSize);
                glUniform1f(highlight_depth_bias_location, detail::kPointDepthBias);
                glUniform1i(highlight_round_points_location, 1);
                glEnable(GL_PROGRAM_POINT_SIZE);
                glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(selected_point_vertex_count_));
                glDisable(GL_PROGRAM_POINT_SIZE);
            }
        }

        glDisable(GL_BLEND);
    });

    frame_timing.axis_pass_ms = measureMilliseconds([&]() {
        glUseProgram(axis_shader_program_);
        const int axis_mvp_location = glGetUniformLocation(axis_shader_program_, "u_mvp");
        glUniformMatrix4fv(axis_mvp_location, 1, GL_FALSE, mvp.data());
        glBindVertexArray(axis_vertex_array_);
        glLineWidth(2.0F);
        glDrawArrays(GL_LINES, 0, 6);
        glBindVertexArray(0);
    });

    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    frame_timing.total_render_ms =
        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - frame_start).count();
    last_frame_timing_ = frame_timing;
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

const ViewportRenderer::FrameTiming& ViewportRenderer::lastFrameTiming() const {
    return last_frame_timing_;
}

const ViewportRenderer::SelectionSummary& ViewportRenderer::selectionSummary() const {
    return selection_summary_;
}

std::size_t ViewportRenderer::SelectionSummary::totalCount() const {
    return edge_count + face_count + point_count;
}

bool ViewportRenderer::UploadedMeshState::matches(const mesh::MeshDocument* document, UpAxis up_axis) const {
    if (document == nullptr) {
        return source_path.empty() &&
               mesh_revision == 0 &&
               vertex_count == 0 &&
               triangle_count == 0 &&
               explicit_edge_count == 0 &&
               source_up_axis == up_axis;
    }

    return source_path == document->source_path &&
           mesh_revision == document->mesh_revision &&
           vertex_count == document->positions.size() &&
           triangle_count == document->triangles.size() &&
           explicit_edge_count == document->explicit_edges.size() &&
           source_up_axis == up_axis;
}

}  // namespace meshtools::render
