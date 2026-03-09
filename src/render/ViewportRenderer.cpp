#include "meshtools/render/ViewportRenderer.h"

#include <algorithm>
#include <cmath>

#include "ViewportRendererDetail.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#endif

namespace meshtools::render {

ViewportRenderer::ViewportRenderer() = default;

ViewportRenderer::~ViewportRenderer() {
    if (preview_face_vertex_buffer_ != 0) {
        glDeleteBuffers(1, &preview_face_vertex_buffer_);
    }
    if (preview_face_vertex_array_ != 0) {
        glDeleteVertexArrays(1, &preview_face_vertex_array_);
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

    const int mvp_location = glGetUniformLocation(shader_program_, "u_mvp");
    const int camera_position_location = glGetUniformLocation(shader_program_, "u_camera_position");
    const int render_mode_location = glGetUniformLocation(shader_program_, "u_render_mode");
    const int point_size_location = glGetUniformLocation(shader_program_, "u_point_size");
    glUniformMatrix4fv(mvp_location, 1, GL_FALSE, mvp.data());
    glUniform3f(camera_position_location, camera_data.eye.x, camera_data.eye.y, camera_data.eye.z);
    glUniform1f(point_size_location, detail::kDefaultPointSize);

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
            glUniform4f(highlight_color_location, 0.93F, 0.59F, 0.18F, 0.56F);
            glUniform1f(highlight_point_size_location, detail::kSelectionPointSize);
            glUniform1f(highlight_depth_bias_location, detail::kFaceDepthBias);
            glUniform1i(highlight_round_points_location, 0);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(selected_face_vertex_count_));
        }

        if (preview_face_vertex_count_ > 0U) {
            glBindVertexArray(preview_face_vertex_array_);
            glUniform4f(highlight_color_location, 0.18F, 0.54F, 0.95F, 0.42F);
            glUniform1f(highlight_point_size_location, detail::kSelectionPointSize);
            glUniform1f(highlight_depth_bias_location, detail::kFaceDepthBias * 0.5F);
            glUniform1i(highlight_round_points_location, 0);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(preview_face_vertex_count_));
        }

        if (selected_edge_vertex_count_ > 0U) {
            glBindVertexArray(selected_edge_vertex_array_);
            glUniform4f(highlight_color_location, 1.0F, 0.52F, 0.04F, 1.0F);
            glUniform1f(highlight_point_size_location, detail::kSelectionPointSize);
            glUniform1f(highlight_depth_bias_location, detail::kEdgeDepthBias);
            glUniform1i(highlight_round_points_location, 0);
            glLineWidth(3.0F);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(selected_edge_vertex_count_));
        }

        if (selected_point_vertex_count_ > 0U) {
            glBindVertexArray(selected_point_vertex_array_);
            glUniform4f(highlight_color_location, 1.0F, 0.52F, 0.04F, 1.0F);
            glUniform1f(highlight_point_size_location, detail::kSelectionPointSize);
            glUniform1f(highlight_depth_bias_location, detail::kPointDepthBias);
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

}  // namespace meshtools::render
