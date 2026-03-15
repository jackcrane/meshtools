#include "meshtools/render/ViewportRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "ViewportRendererDetail.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#endif

namespace meshtools::render {
namespace {

constexpr float kEdgeLoopRidgeMinAngleDegrees = 5.0F;
constexpr float kEdgeLoopCoplanarTolerancePercent = 0.01F;

bool containsIndex(const std::vector<std::uint32_t>& indices, std::uint32_t index) {
    return std::find(indices.begin(), indices.end(), index) != indices.end();
}

void normalizeSelectionIndices(std::vector<std::uint32_t>& indices, std::size_t max_count) {
    indices.erase(
        std::remove_if(indices.begin(), indices.end(), [max_count](std::uint32_t index) {
            return index >= max_count;
        }),
        indices.end()
    );
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
}

void addMissingIndices(std::vector<std::uint32_t>& target, const std::vector<std::uint32_t>& hits) {
    for (const std::uint32_t hit_index : hits) {
        if (!containsIndex(target, hit_index)) {
            target.push_back(hit_index);
        }
    }
}

std::vector<std::uint32_t> findShortestPath(
    const std::vector<std::vector<std::uint32_t>>& neighbors,
    std::uint32_t origin,
    std::uint32_t destination
) {
    if (origin >= neighbors.size() || destination >= neighbors.size()) {
        return {};
    }

    if (origin == destination) {
        return {origin};
    }

    std::vector<std::int32_t> previous(neighbors.size(), -1);
    std::deque<std::uint32_t> frontier;
    frontier.push_back(origin);
    previous[origin] = static_cast<std::int32_t>(origin);

    while (!frontier.empty()) {
        const std::uint32_t current = frontier.front();
        frontier.pop_front();

        for (const std::uint32_t neighbor : neighbors[current]) {
            if (neighbor >= neighbors.size() || previous[neighbor] != -1) {
                continue;
            }

            previous[neighbor] = static_cast<std::int32_t>(current);
            if (neighbor == destination) {
                frontier.clear();
                break;
            }

            frontier.push_back(neighbor);
        }
    }

    if (previous[destination] == -1) {
        return {};
    }

    std::vector<std::uint32_t> path;
    for (std::uint32_t current = destination; current != origin; current = static_cast<std::uint32_t>(previous[current])) {
        path.push_back(current);
    }
    path.push_back(origin);
    std::reverse(path.begin(), path.end());
    return path;
}

struct FaceAnalysis {
    mesh::Vec3 centroid;
    mesh::Vec3 normal;
    float plane_offset = 0.0F;
};

struct PointTarget {
    bool valid = false;
    bool too_far = false;
    mesh::Vec3 point;
};

struct LineTarget {
    bool valid = false;
    bool too_far = false;
    mesh::Vec3 point;
    mesh::Vec3 direction;
};

struct ClosestLinePoints {
    bool valid = false;
    float first_parameter = 0.0F;
    float second_parameter = 0.0F;
    mesh::Vec3 first_point;
    mesh::Vec3 second_point;
    float distance = 0.0F;
};

float modelDiagonalLength(const std::vector<mesh::Vec3>& positions) {
    if (positions.empty()) {
        return 1.0F;
    }

    mesh::Vec3 minimum = positions.front();
    mesh::Vec3 maximum = positions.front();
    for (const mesh::Vec3& position : positions) {
        minimum.x = std::min(minimum.x, position.x);
        minimum.y = std::min(minimum.y, position.y);
        minimum.z = std::min(minimum.z, position.z);
        maximum.x = std::max(maximum.x, position.x);
        maximum.y = std::max(maximum.y, position.y);
        maximum.z = std::max(maximum.z, position.z);
    }

    return detail::length(detail::subtract(maximum, minimum));
}

std::vector<FaceAnalysis> buildFaceAnalysis(
    const std::vector<mesh::Vec3>& centroids,
    const std::vector<mesh::Vec3>& normals,
    const std::vector<float>& plane_offsets
) {
    std::vector<FaceAnalysis> analysis;
    analysis.reserve(centroids.size());
    for (std::size_t face_index = 0; face_index < centroids.size(); ++face_index) {
        analysis.push_back(FaceAnalysis{
            .centroid = centroids[face_index],
            .normal = normals[face_index],
            .plane_offset = plane_offsets[face_index],
        });
    }
    return analysis;
}

mesh::EntitySelection makeExpandedFaceSelection(
    const mesh::EntitySelection& current_selection,
    std::vector<std::uint32_t> expanded_faces,
    std::size_t max_face_count
) {
    normalizeSelectionIndices(expanded_faces, max_face_count);
    mesh::EntitySelection selection = current_selection;
    selection.face_indices = std::move(expanded_faces);
    return selection;
}

std::vector<std::uint32_t> collectPreviewFaces(
    const std::vector<std::uint32_t>& current_faces,
    const std::vector<std::uint32_t>& expanded_faces
) {
    std::vector<std::uint32_t> preview_faces;
    preview_faces.reserve(expanded_faces.size());
    std::vector<unsigned char> current_face_mask;
    if (!expanded_faces.empty()) {
        current_face_mask.assign(*std::max_element(expanded_faces.begin(), expanded_faces.end()) + 1U, 0);
        for (const std::uint32_t face_index : current_faces) {
            if (face_index < current_face_mask.size()) {
                current_face_mask[face_index] = 1;
            }
        }
    }
    for (const std::uint32_t face_index : expanded_faces) {
        if (face_index >= current_face_mask.size() || current_face_mask[face_index] == 0) {
            preview_faces.push_back(face_index);
        }
    }
    return preview_faces;
}

bool indexVectorsEqual(const std::vector<std::uint32_t>& left, const std::vector<std::uint32_t>& right) {
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
}

bool solve3x3(
    const std::array<std::array<float, 3>, 3>& matrix,
    const std::array<float, 3>& right_hand_side,
    mesh::Vec3* solution
) {
    const float determinant =
        (matrix[0][0] * ((matrix[1][1] * matrix[2][2]) - (matrix[1][2] * matrix[2][1]))) -
        (matrix[0][1] * ((matrix[1][0] * matrix[2][2]) - (matrix[1][2] * matrix[2][0]))) +
        (matrix[0][2] * ((matrix[1][0] * matrix[2][1]) - (matrix[1][1] * matrix[2][0])));
    if (std::abs(determinant) <= 1.0e-6F || solution == nullptr) {
        return false;
    }

    auto determinant_with_column = [&matrix](const std::array<float, 3>& column, int replace_column) {
        std::array<std::array<float, 3>, 3> modified = matrix;
        const std::size_t column_index = static_cast<std::size_t>(replace_column);
        for (std::size_t row = 0; row < 3U; ++row) {
            modified[row][column_index] = column[row];
        }
        return
            (modified[0][0] * ((modified[1][1] * modified[2][2]) - (modified[1][2] * modified[2][1]))) -
            (modified[0][1] * ((modified[1][0] * modified[2][2]) - (modified[1][2] * modified[2][0]))) +
            (modified[0][2] * ((modified[1][0] * modified[2][1]) - (modified[1][1] * modified[2][0])));
    };

    solution->x = determinant_with_column(right_hand_side, 0) / determinant;
    solution->y = determinant_with_column(right_hand_side, 1) / determinant;
    solution->z = determinant_with_column(right_hand_side, 2) / determinant;
    return true;
}

bool pointMatchesNormalTarget(
    const FaceAnalysis& face,
    const mesh::Vec3& target,
    float tolerance,
    bool include_inverse_normals
) {
    const mesh::Vec3 delta = detail::subtract(target, face.centroid);
    const float along_normal = detail::dot(delta, face.normal);
    if (!include_inverse_normals && along_normal < (-tolerance)) {
        return false;
    }

    const mesh::Vec3 projected = detail::scale(face.normal, along_normal);
    const mesh::Vec3 perpendicular = detail::subtract(delta, projected);
    return detail::length(perpendicular) <= tolerance;
}

ClosestLinePoints closestPointsBetweenLines(
    const mesh::Vec3& first_point,
    const mesh::Vec3& first_direction,
    const mesh::Vec3& second_point,
    const mesh::Vec3& second_direction
) {
    const mesh::Vec3 delta = detail::subtract(first_point, second_point);
    const float first_dot_first = detail::dot(first_direction, first_direction);
    const float first_dot_second = detail::dot(first_direction, second_direction);
    const float second_dot_second = detail::dot(second_direction, second_direction);
    const float first_dot_delta = detail::dot(first_direction, delta);
    const float second_dot_delta = detail::dot(second_direction, delta);
    const float denominator =
        (first_dot_first * second_dot_second) - (first_dot_second * first_dot_second);
    if (std::abs(denominator) <= 1.0e-6F) {
        return {};
    }

    const float first_parameter =
        ((first_dot_second * second_dot_delta) - (second_dot_second * first_dot_delta)) / denominator;
    const float second_parameter =
        ((first_dot_first * second_dot_delta) - (first_dot_second * first_dot_delta)) / denominator;
    const mesh::Vec3 closest_first = detail::add(first_point, detail::scale(first_direction, first_parameter));
    const mesh::Vec3 closest_second = detail::add(second_point, detail::scale(second_direction, second_parameter));
    return ClosestLinePoints{
        .valid = true,
        .first_parameter = first_parameter,
        .second_parameter = second_parameter,
        .first_point = closest_first,
        .second_point = closest_second,
        .distance = detail::length(detail::subtract(closest_first, closest_second)),
    };
}

bool lineMatchesNormalTarget(
    const FaceAnalysis& face,
    const mesh::Vec3& target_point,
    const mesh::Vec3& target_direction,
    float tolerance,
    bool include_inverse_normals
) {
    const ClosestLinePoints closest = closestPointsBetweenLines(
        face.centroid,
        face.normal,
        target_point,
        target_direction
    );
    if (closest.valid) {
        if (closest.distance > tolerance) {
            return false;
        }
        return include_inverse_normals || closest.first_parameter >= (-tolerance);
    }

    const mesh::Vec3 delta = detail::subtract(target_point, face.centroid);
    const float along_normal = detail::dot(delta, face.normal);
    const mesh::Vec3 perpendicular = detail::subtract(delta, detail::scale(face.normal, along_normal));
    if (detail::length(perpendicular) > tolerance) {
        return false;
    }
    return include_inverse_normals || along_normal >= (-tolerance);
}

bool facesMatchCoplanar(
    const FaceAnalysis& candidate_face,
    const FaceAnalysis& reference_face,
    float tolerance_distance,
    float normal_alignment_tolerance,
    bool include_parallel
) {
    const float normal_dot = detail::dot(candidate_face.normal, reference_face.normal);
    if ((1.0F - std::abs(normal_dot)) > normal_alignment_tolerance) {
        return false;
    }

    if (!include_parallel) {
        const float orientation = normal_dot >= 0.0F ? 1.0F : -1.0F;
        const float plane_distance = std::abs(
            reference_face.plane_offset - (candidate_face.plane_offset * orientation)
        );
        if (plane_distance > tolerance_distance) {
            return false;
        }
    }

    return true;
}

PointTarget findCommonPointTarget(
    const std::vector<FaceAnalysis>& faces,
    const std::vector<std::uint32_t>& selected_faces,
    float tolerance,
    bool include_inverse_normals,
    float max_distance_from_model
) {
    if (selected_faces.size() < 2U) {
        return {};
    }

    std::array<std::array<float, 3>, 3> system{};
    std::array<float, 3> right_hand_side{};
    for (const std::uint32_t face_index : selected_faces) {
        if (face_index >= faces.size()) {
            continue;
        }

        const mesh::Vec3& direction = faces[face_index].normal;
        const float dx = direction.x;
        const float dy = direction.y;
        const float dz = direction.z;
        const std::array<std::array<float, 3>, 3> projection = {{
            {{1.0F - (dx * dx), -(dx * dy), -(dx * dz)}},
            {{-(dy * dx), 1.0F - (dy * dy), -(dy * dz)}},
            {{-(dz * dx), -(dz * dy), 1.0F - (dz * dz)}},
        }};

        system[0][0] += projection[0][0];
        system[0][1] += projection[0][1];
        system[0][2] += projection[0][2];
        system[1][0] += projection[1][0];
        system[1][1] += projection[1][1];
        system[1][2] += projection[1][2];
        system[2][0] += projection[2][0];
        system[2][1] += projection[2][1];
        system[2][2] += projection[2][2];

        right_hand_side[0] +=
            (projection[0][0] * faces[face_index].centroid.x) +
            (projection[0][1] * faces[face_index].centroid.y) +
            (projection[0][2] * faces[face_index].centroid.z);
        right_hand_side[1] +=
            (projection[1][0] * faces[face_index].centroid.x) +
            (projection[1][1] * faces[face_index].centroid.y) +
            (projection[1][2] * faces[face_index].centroid.z);
        right_hand_side[2] +=
            (projection[2][0] * faces[face_index].centroid.x) +
            (projection[2][1] * faces[face_index].centroid.y) +
            (projection[2][2] * faces[face_index].centroid.z);
    }

    mesh::Vec3 solution{};
    if (!solve3x3(system, right_hand_side, &solution)) {
        return {};
    }

    if (detail::length(solution) > max_distance_from_model) {
        return PointTarget{
            .valid = false,
            .too_far = true,
            .point = solution,
        };
    }

    for (const std::uint32_t face_index : selected_faces) {
        if (face_index >= faces.size()) {
            continue;
        }
        if (!pointMatchesNormalTarget(faces[face_index], solution, tolerance, include_inverse_normals)) {
            return {};
        }
    }

    return PointTarget{
        .valid = true,
        .point = solution,
    };
}

LineTarget findCommonLineTarget(
    const std::vector<FaceAnalysis>& faces,
    const std::vector<std::uint32_t>& selected_faces,
    float tolerance,
    bool include_inverse_normals,
    float max_distance_from_model
) {
    if (selected_faces.size() < 4U) {
        return {};
    }

    struct Cluster {
        mesh::Vec3 center{};
        std::size_t count = 0;
    };

    std::vector<Cluster> clusters;
    const float cluster_radius = std::max(tolerance * 2.0F, 0.01F);
    for (std::size_t left_index = 0; left_index < selected_faces.size(); ++left_index) {
        for (std::size_t right_index = left_index + 1U; right_index < selected_faces.size(); ++right_index) {
            const std::uint32_t first_face_index = selected_faces[left_index];
            const std::uint32_t second_face_index = selected_faces[right_index];
            if (first_face_index >= faces.size() || second_face_index >= faces.size()) {
                continue;
            }

            const ClosestLinePoints closest = closestPointsBetweenLines(
                faces[first_face_index].centroid,
                faces[first_face_index].normal,
                faces[second_face_index].centroid,
                faces[second_face_index].normal
            );
            if (!closest.valid || closest.distance > tolerance) {
                continue;
            }
            if (!include_inverse_normals &&
                (closest.first_parameter < (-tolerance) || closest.second_parameter < (-tolerance))) {
                continue;
            }

            const mesh::Vec3 candidate = detail::scale(detail::add(closest.first_point, closest.second_point), 0.5F);
            if (detail::length(candidate) > max_distance_from_model) {
                continue;
            }

            auto existing_cluster = std::find_if(clusters.begin(), clusters.end(), [&](const Cluster& cluster) {
                return detail::length(detail::subtract(cluster.center, candidate)) <= cluster_radius;
            });
            if (existing_cluster == clusters.end()) {
                clusters.push_back(Cluster{
                    .center = candidate,
                    .count = 1U,
                });
            } else {
                const float weight = static_cast<float>(existing_cluster->count);
                existing_cluster->center = detail::scale(
                    detail::add(detail::scale(existing_cluster->center, weight), candidate),
                    1.0F / (weight + 1.0F)
                );
                ++existing_cluster->count;
            }
        }
    }

    std::sort(clusters.begin(), clusters.end(), [](const Cluster& left, const Cluster& right) {
        return left.count > right.count;
    });

    for (std::size_t first_cluster_index = 0; first_cluster_index < clusters.size(); ++first_cluster_index) {
        for (std::size_t second_cluster_index = first_cluster_index + 1U; second_cluster_index < clusters.size(); ++second_cluster_index) {
            const mesh::Vec3 delta = detail::subtract(
                clusters[second_cluster_index].center,
                clusters[first_cluster_index].center
            );
            if (detail::length(delta) <= (cluster_radius * 2.0F)) {
                continue;
            }

            const mesh::Vec3 direction = detail::normalize(delta);
            bool matches_all_selected = true;
            for (const std::uint32_t face_index : selected_faces) {
                if (face_index >= faces.size()) {
                    continue;
                }
                if (!lineMatchesNormalTarget(
                        faces[face_index],
                        clusters[first_cluster_index].center,
                        direction,
                        tolerance,
                        include_inverse_normals
                    )) {
                    matches_all_selected = false;
                    break;
                }
            }

            if (matches_all_selected) {
                return LineTarget{
                    .valid = true,
                    .point = clusters[first_cluster_index].center,
                    .direction = direction,
                };
            }
        }
    }

    return {};
}

}  // namespace

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
    face_vertices.reserve(selected_face_indices_.size() * 3ULL);
    for (const std::uint32_t triangle_index : selected_face_indices_) {
        if (triangle_index >= normalized_triangles_.size()) {
            continue;
        }

        const mesh::Triangle& triangle = normalized_triangles_[triangle_index];
        face_vertices.push_back(HighlightVertex{{normalized_positions_[triangle.a].x, normalized_positions_[triangle.a].y, normalized_positions_[triangle.a].z}});
        face_vertices.push_back(HighlightVertex{{normalized_positions_[triangle.b].x, normalized_positions_[triangle.b].y, normalized_positions_[triangle.b].z}});
        face_vertices.push_back(HighlightVertex{{normalized_positions_[triangle.c].x, normalized_positions_[triangle.c].y, normalized_positions_[triangle.c].z}});
    }
    selected_face_vertex_count_ = static_cast<std::uint32_t>(face_vertices.size());
    upload_highlight_geometry(selected_face_vertex_array_, selected_face_vertex_buffer_, face_vertices);

    std::vector<HighlightVertex> preview_face_vertices;
    preview_face_vertices.reserve(preview_face_indices_.size() * 3ULL);
    for (const std::uint32_t triangle_index : preview_face_indices_) {
        if (triangle_index >= normalized_triangles_.size()) {
            continue;
        }

        const mesh::Triangle& triangle = normalized_triangles_[triangle_index];
        preview_face_vertices.push_back(HighlightVertex{{normalized_positions_[triangle.a].x, normalized_positions_[triangle.a].y, normalized_positions_[triangle.a].z}});
        preview_face_vertices.push_back(HighlightVertex{{normalized_positions_[triangle.b].x, normalized_positions_[triangle.b].y, normalized_positions_[triangle.b].z}});
        preview_face_vertices.push_back(HighlightVertex{{normalized_positions_[triangle.c].x, normalized_positions_[triangle.c].y, normalized_positions_[triangle.c].z}});
    }
    preview_face_vertex_count_ = static_cast<std::uint32_t>(preview_face_vertices.size());
    upload_highlight_geometry(preview_face_vertex_array_, preview_face_vertex_buffer_, preview_face_vertices);

    std::vector<HighlightVertex> preview_edge_vertices;
    preview_edge_vertices.reserve(preview_edge_indices_.size() * 2ULL);
    for (const std::uint32_t edge_index : preview_edge_indices_) {
        if (edge_index >= unique_edges_.size()) {
            continue;
        }

        const Edge& edge = unique_edges_[edge_index];
        preview_edge_vertices.push_back(HighlightVertex{{normalized_positions_[edge.a].x, normalized_positions_[edge.a].y, normalized_positions_[edge.a].z}});
        preview_edge_vertices.push_back(HighlightVertex{{normalized_positions_[edge.b].x, normalized_positions_[edge.b].y, normalized_positions_[edge.b].z}});
    }
    preview_edge_vertex_count_ = static_cast<std::uint32_t>(preview_edge_vertices.size());
    upload_highlight_geometry(preview_edge_vertex_array_, preview_edge_vertex_buffer_, preview_edge_vertices);

    std::vector<HighlightVertex> edge_vertices;
    edge_vertices.reserve(selected_edge_indices_.size() * 2ULL);
    for (const std::uint32_t edge_index : selected_edge_indices_) {
        if (edge_index >= unique_edges_.size()) {
            continue;
        }

        const Edge& edge = unique_edges_[edge_index];
        edge_vertices.push_back(HighlightVertex{{normalized_positions_[edge.a].x, normalized_positions_[edge.a].y, normalized_positions_[edge.a].z}});
        edge_vertices.push_back(HighlightVertex{{normalized_positions_[edge.b].x, normalized_positions_[edge.b].y, normalized_positions_[edge.b].z}});
    }
    selected_edge_vertex_count_ = static_cast<std::uint32_t>(edge_vertices.size());
    upload_highlight_geometry(selected_edge_vertex_array_, selected_edge_vertex_buffer_, edge_vertices);

    std::vector<HighlightVertex> point_vertices;
    point_vertices.reserve(selected_point_indices_.size());
    for (const std::uint32_t point_index : selected_point_indices_) {
        if (point_index >= normalized_positions_.size()) {
            continue;
        }

        const mesh::Vec3& point = normalized_positions_[point_index];
        point_vertices.push_back(HighlightVertex{{point.x, point.y, point.z}});
    }
    selected_point_vertex_count_ = static_cast<std::uint32_t>(point_vertices.size());
    upload_highlight_geometry(selected_point_vertex_array_, selected_point_vertex_buffer_, point_vertices);

    glBindVertexArray(0);
}

std::size_t ViewportRenderer::selectAt(
    float normalized_x,
    float normalized_y,
    const SelectionQuery& selection_query,
    SelectionMode selection_mode
) {
    if (!has_document_mesh_ ||
        normalized_positions_.empty() ||
        normalized_triangles_.empty() ||
        framebuffer_width_ <= 0 ||
        framebuffer_height_ <= 0) {
        if (selection_mode == SelectionMode::Replace) {
            clearSelection();
            return 0;
        }

        return selection_summary_.totalCount();
    }

    if (!selection_query.edges && !selection_query.faces && !selection_query.points) {
        if (selection_mode == SelectionMode::Replace) {
            clearSelection();
            return 0;
        }

        return selection_summary_.totalCount();
    }

    const float aspect_ratio = static_cast<float>(framebuffer_width_) / static_cast<float>(framebuffer_height_);
    const detail::CameraData camera_data = detail::makeCameraData(camera_);
    const mesh::Vec3 ray_direction = detail::makeRayDirection(camera_data, normalized_x, normalized_y, aspect_ratio);

    const detail::HitCandidate front_face_hit = detail::findNearestTriangleHit(
        normalized_positions_,
        normalized_triangles_,
        camera_data.eye,
        ray_direction
    );
    detail::HitCandidate edge_hit;
    detail::HitCandidate point_hit;

    if (selection_query.edges) {
        edge_hit = detail::findNearestEdgeHit(
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
        point_hit = detail::findNearestPointHit(
            normalized_positions_,
            camera_data.eye,
            ray_direction,
            aspect_ratio,
            framebuffer_width_,
            framebuffer_height_
        );
    }

    const float occlusion_limit = front_face_hit.hit
        ? (front_face_hit.t + detail::kSelectionDepthTolerance)
        : std::numeric_limits<float>::infinity();

    if (edge_hit.hit && edge_hit.t > occlusion_limit) {
        edge_hit = detail::HitCandidate{};
    }
    if (point_hit.hit && point_hit.t > occlusion_limit) {
        point_hit = detail::HitCandidate{};
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
        if (selection_mode == SelectionMode::Replace) {
            clearSelection();
            return 0;
        }

        return selection_summary_.totalCount();
    }

    std::vector<std::uint32_t> clicked_face_indices;
    std::vector<std::uint32_t> clicked_edge_indices;
    std::vector<std::uint32_t> clicked_point_indices;
    if (face_selectable && std::abs(front_face_hit.t - front_t) <= detail::kSelectionDepthTolerance) {
        clicked_face_indices.push_back(front_face_hit.index);
    }
    if (edge_hit.hit && std::abs(edge_hit.t - front_t) <= detail::kSelectionDepthTolerance) {
        clicked_edge_indices.push_back(edge_hit.index);
    }
    if (point_hit.hit && std::abs(point_hit.t - front_t) <= detail::kSelectionDepthTolerance) {
        clicked_point_indices.push_back(point_hit.index);
    }

    if (selection_mode == SelectionMode::Replace) {
        selected_face_indices_ = std::move(clicked_face_indices);
        selected_edge_indices_ = std::move(clicked_edge_indices);
        selected_point_indices_ = std::move(clicked_point_indices);
        face_selection_anchor_ =
            selected_face_indices_.empty() ? std::nullopt : std::optional<std::uint32_t>(selected_face_indices_.front());
        edge_selection_anchor_ =
            selected_edge_indices_.empty() ? std::nullopt : std::optional<std::uint32_t>(selected_edge_indices_.front());
    } else if (selection_mode == SelectionMode::Toggle) {
        auto toggle_indices = [](std::vector<std::uint32_t>& target, const std::vector<std::uint32_t>& hits) {
            for (const std::uint32_t hit_index : hits) {
                const auto existing = std::find(target.begin(), target.end(), hit_index);
                if (existing != target.end()) {
                    target.erase(existing);
                } else {
                    target.push_back(hit_index);
                }
            }
        };

        toggle_indices(selected_face_indices_, clicked_face_indices);
        toggle_indices(selected_edge_indices_, clicked_edge_indices);
        toggle_indices(selected_point_indices_, clicked_point_indices);
        if (!clicked_face_indices.empty()) {
            if (containsIndex(selected_face_indices_, clicked_face_indices.front())) {
                face_selection_anchor_ = clicked_face_indices.front();
            } else if (face_selection_anchor_ == clicked_face_indices.front()) {
                face_selection_anchor_.reset();
            }
        }
        if (!clicked_edge_indices.empty()) {
            if (containsIndex(selected_edge_indices_, clicked_edge_indices.front())) {
                edge_selection_anchor_ = clicked_edge_indices.front();
            } else if (edge_selection_anchor_ == clicked_edge_indices.front()) {
                edge_selection_anchor_.reset();
            }
        }
    } else {
        auto add_path_or_fallback = [](const char* label,
                                       std::vector<std::uint32_t>& target,
                                       const std::vector<std::vector<std::uint32_t>>& neighbors,
                                       std::optional<std::uint32_t>& anchor,
                                       const std::vector<std::uint32_t>& hits) {
            if (hits.empty()) {
                return;
            }

            for (const std::uint32_t hit_index : hits) {
                if (anchor.has_value()) {
                    const std::vector<std::uint32_t> path = findShortestPath(neighbors, *anchor, hit_index);
                    if (!path.empty()) {
                        addMissingIndices(target, path);
                        std::cout
                            << "[selection:path] " << label
                            << " anchor=" << *anchor
                            << " destination=" << hit_index
                            << " path_length=" << path.size()
                            << '\n';
                    } else {
                        addMissingIndices(target, {hit_index});
                        std::cout
                            << "[selection:path] " << label
                            << " anchor=" << *anchor
                            << " destination=" << hit_index
                            << " disconnected_fallback=1\n";
                    }
                } else {
                    addMissingIndices(target, {hit_index});
                    std::cout
                        << "[selection:path] " << label
                        << " anchor=none"
                        << " destination=" << hit_index
                        << " seeded=1\n";
                }

                if (containsIndex(target, hit_index)) {
                    anchor = hit_index;
                }
            }
        };

        add_path_or_fallback("faces", selected_face_indices_, face_neighbors_, face_selection_anchor_, clicked_face_indices);
        add_path_or_fallback("edges", selected_edge_indices_, edge_neighbors_, edge_selection_anchor_, clicked_edge_indices);
        addMissingIndices(selected_point_indices_, clicked_point_indices);
    }

    selection_summary_ = SelectionSummary{
        .edge_count = selected_edge_indices_.size(),
        .face_count = selected_face_indices_.size(),
        .point_count = selected_point_indices_.size(),
    };
    edge_loop_cycle_ = {};
    updateHighlightBuffers();
    return selection_summary_.totalCount();
}

std::size_t ViewportRenderer::selectInRect(
    float normalized_min_x,
    float normalized_min_y,
    float normalized_max_x,
    float normalized_max_y,
    const SelectionQuery& selection_query,
    SelectionMode selection_mode
) {
    if (!has_document_mesh_ ||
        normalized_positions_.empty() ||
        normalized_triangles_.empty() ||
        framebuffer_width_ <= 0 ||
        framebuffer_height_ <= 0) {
        if (selection_mode == SelectionMode::Replace) {
            clearSelection();
            return 0;
        }

        return selection_summary_.totalCount();
    }

    if (!selection_query.edges && !selection_query.faces && !selection_query.points) {
        if (selection_mode == SelectionMode::Replace) {
            clearSelection();
            return 0;
        }

        return selection_summary_.totalCount();
    }

    const float min_x = std::clamp(std::min(normalized_min_x, normalized_max_x), 0.0F, 1.0F);
    const float min_y = std::clamp(std::min(normalized_min_y, normalized_max_y), 0.0F, 1.0F);
    const float max_x = std::clamp(std::max(normalized_min_x, normalized_max_x), 0.0F, 1.0F);
    const float max_y = std::clamp(std::max(normalized_min_y, normalized_max_y), 0.0F, 1.0F);
    const float aspect_ratio = static_cast<float>(framebuffer_width_) / static_cast<float>(framebuffer_height_);
    const detail::CameraData camera_data = detail::makeCameraData(camera_);
    const std::vector<float> depth_buffer = detail::readDepthBuffer(framebuffer_, framebuffer_width_, framebuffer_height_);
    std::vector<detail::ProjectedPoint> projected_positions(normalized_positions_.size());
    for (std::size_t index = 0; index < normalized_positions_.size(); ++index) {
        projected_positions[index] = detail::projectPointToViewport(normalized_positions_[index], camera_data, aspect_ratio);
    }

    std::vector<std::uint32_t> box_face_indices;
    std::vector<std::uint32_t> box_edge_indices;
    std::vector<std::uint32_t> box_point_indices;

    if (selection_query.faces) {
        for (std::size_t triangle_index = 0; triangle_index < normalized_triangles_.size(); ++triangle_index) {
            const mesh::Triangle& triangle = normalized_triangles_[triangle_index];
            const detail::ProjectedPoint& projected_a = projected_positions[triangle.a];
            const detail::ProjectedPoint& projected_b = projected_positions[triangle.b];
            const detail::ProjectedPoint& projected_c = projected_positions[triangle.c];
            if (!projected_a.valid || !projected_b.valid || !projected_c.valid) {
                continue;
            }

            if (!detail::triangleIntersectsRect(
                    detail::Vec2{projected_a.normalized_x, projected_a.normalized_y},
                    detail::Vec2{projected_b.normalized_x, projected_b.normalized_y},
                    detail::Vec2{projected_c.normalized_x, projected_c.normalized_y},
                    min_x,
                    min_y,
                    max_x,
                    max_y
                )) {
                continue;
            }

            const mesh::Vec3 centroid = detail::scale(
                detail::add(detail::add(normalized_positions_[triangle.a], normalized_positions_[triangle.b]), normalized_positions_[triangle.c]),
                1.0F / 3.0F
            );
            const detail::ProjectedPoint projected_centroid = detail::projectPointToViewport(centroid, camera_data, aspect_ratio);
            if (!detail::isProjectedPointVisible(projected_centroid, depth_buffer, framebuffer_width_, framebuffer_height_)) {
                continue;
            }

            box_face_indices.push_back(static_cast<std::uint32_t>(triangle_index));
        }
    }

    if (selection_query.edges) {
        for (std::size_t edge_index = 0; edge_index < unique_edges_.size(); ++edge_index) {
            const Edge& edge = unique_edges_[edge_index];
            const detail::ProjectedPoint& projected_a = projected_positions[edge.a];
            const detail::ProjectedPoint& projected_b = projected_positions[edge.b];
            if (!projected_a.valid || !projected_b.valid) {
                continue;
            }

            if (!detail::segmentIntersectsRect(
                    detail::Vec2{projected_a.normalized_x, projected_a.normalized_y},
                    detail::Vec2{projected_b.normalized_x, projected_b.normalized_y},
                    min_x,
                    min_y,
                    max_x,
                    max_y
                )) {
                continue;
            }

            const mesh::Vec3 midpoint = detail::scale(detail::add(normalized_positions_[edge.a], normalized_positions_[edge.b]), 0.5F);
            const detail::ProjectedPoint projected_midpoint = detail::projectPointToViewport(midpoint, camera_data, aspect_ratio);
            if (!detail::isProjectedPointVisible(projected_midpoint, depth_buffer, framebuffer_width_, framebuffer_height_)) {
                continue;
            }

            box_edge_indices.push_back(static_cast<std::uint32_t>(edge_index));
        }
    }

    if (selection_query.points) {
        for (std::size_t point_index = 0; point_index < normalized_positions_.size(); ++point_index) {
            const detail::ProjectedPoint& projected_point = projected_positions[point_index];
            if (!projected_point.valid ||
                !detail::pointInNormalizedRect(projected_point.normalized_x, projected_point.normalized_y, min_x, min_y, max_x, max_y)) {
                continue;
            }

            if (!detail::isProjectedPointVisible(projected_point, depth_buffer, framebuffer_width_, framebuffer_height_)) {
                continue;
            }

            box_point_indices.push_back(static_cast<std::uint32_t>(point_index));
        }
    }

    if (selection_mode == SelectionMode::Replace) {
        selected_face_indices_ = std::move(box_face_indices);
        selected_edge_indices_ = std::move(box_edge_indices);
        selected_point_indices_ = std::move(box_point_indices);
    } else {
        const bool any_hits =
            !box_face_indices.empty() ||
            !box_edge_indices.empty() ||
            !box_point_indices.empty();

        auto contains_all = [](const std::vector<std::uint32_t>& target, const std::vector<std::uint32_t>& hits) {
            return std::all_of(hits.begin(), hits.end(), [&target](std::uint32_t hit_index) {
                return std::find(target.begin(), target.end(), hit_index) != target.end();
            });
        };

        const bool all_hits_already_selected =
            any_hits &&
            contains_all(selected_face_indices_, box_face_indices) &&
            contains_all(selected_edge_indices_, box_edge_indices) &&
            contains_all(selected_point_indices_, box_point_indices);

        auto remove_hits = [](std::vector<std::uint32_t>& target, const std::vector<std::uint32_t>& hits) {
            target.erase(
                std::remove_if(target.begin(), target.end(), [&hits](std::uint32_t value) {
                    return std::find(hits.begin(), hits.end(), value) != hits.end();
                }),
                target.end()
            );
        };
        auto add_missing_hits = [](std::vector<std::uint32_t>& target, const std::vector<std::uint32_t>& hits) {
            for (const std::uint32_t hit_index : hits) {
                if (std::find(target.begin(), target.end(), hit_index) == target.end()) {
                    target.push_back(hit_index);
                }
            }
        };

        if (all_hits_already_selected) {
            remove_hits(selected_face_indices_, box_face_indices);
            remove_hits(selected_edge_indices_, box_edge_indices);
            remove_hits(selected_point_indices_, box_point_indices);
        } else if (any_hits) {
            add_missing_hits(selected_face_indices_, box_face_indices);
            add_missing_hits(selected_edge_indices_, box_edge_indices);
            add_missing_hits(selected_point_indices_, box_point_indices);
        }
    }

    face_selection_anchor_.reset();
    edge_selection_anchor_.reset();

    selection_summary_ = SelectionSummary{
        .edge_count = selected_edge_indices_.size(),
        .face_count = selected_face_indices_.size(),
        .point_count = selected_point_indices_.size(),
    };
    edge_loop_cycle_ = {};
    updateHighlightBuffers();
    return selection_summary_.totalCount();
}

std::size_t ViewportRenderer::invertSelection(const SelectionQuery& selection_query) {
    if (!has_document_mesh_) {
        return selection_summary_.totalCount();
    }

    auto invert_indices = [](std::vector<std::uint32_t>& selected_indices, std::size_t total_count) {
        std::vector<unsigned char> selected_lookup(total_count, 0);
        for (const std::uint32_t selected_index : selected_indices) {
            if (selected_index < total_count) {
                selected_lookup[selected_index] = 1;
            }
        }

        std::vector<std::uint32_t> inverted_indices;
        inverted_indices.reserve(total_count > selected_indices.size() ? (total_count - selected_indices.size()) : 0);
        for (std::size_t index = 0; index < total_count; ++index) {
            if (selected_lookup[index] == 0) {
                inverted_indices.push_back(static_cast<std::uint32_t>(index));
            }
        }

        selected_indices = std::move(inverted_indices);
    };

    if (selection_query.faces) {
        invert_indices(selected_face_indices_, normalized_triangles_.size());
    }
    if (selection_query.edges) {
        invert_indices(selected_edge_indices_, unique_edges_.size());
    }
    if (selection_query.points) {
        invert_indices(selected_point_indices_, normalized_positions_.size());
    }

    face_selection_anchor_.reset();
    edge_selection_anchor_.reset();

    selection_summary_ = SelectionSummary{
        .edge_count = selected_edge_indices_.size(),
        .face_count = selected_face_indices_.size(),
        .point_count = selected_point_indices_.size(),
    };
    edge_loop_cycle_ = {};
    updateHighlightBuffers();
    return selection_summary_.totalCount();
}

void ViewportRenderer::clearSelection() {
    selected_edge_indices_.clear();
    selected_face_indices_.clear();
    selected_point_indices_.clear();
    preview_face_indices_.clear();
    preview_edge_indices_.clear();
    face_selection_anchor_.reset();
    edge_selection_anchor_.reset();
    edge_loop_cycle_ = {};
    selection_summary_ = SelectionSummary{};
    updateHighlightBuffers();
}

mesh::EntitySelection ViewportRenderer::currentSelection() const {
    return mesh::EntitySelection{
        .edge_indices = selected_edge_indices_,
        .face_indices = selected_face_indices_,
        .point_indices = selected_point_indices_,
    };
}

void ViewportRenderer::setSelection(mesh::EntitySelection selection) {
    normalizeSelectionIndices(selection.edge_indices, unique_edges_.size());
    normalizeSelectionIndices(selection.face_indices, normalized_triangles_.size());
    normalizeSelectionIndices(selection.point_indices, normalized_positions_.size());

    selected_edge_indices_ = std::move(selection.edge_indices);
    selected_face_indices_ = std::move(selection.face_indices);
    selected_point_indices_ = std::move(selection.point_indices);
    face_selection_anchor_.reset();
    edge_selection_anchor_.reset();
    edge_loop_cycle_ = {};
    selection_summary_ = SelectionSummary{
        .edge_count = selected_edge_indices_.size(),
        .face_count = selected_face_indices_.size(),
        .point_count = selected_point_indices_.size(),
    };
    updateHighlightBuffers();
}

ViewportRenderer::EdgeLoopSelectionResult ViewportRenderer::selectEdgeLoop() {
    EdgeLoopSelectionResult result;
    result.selection = currentSelection();

    if (!has_document_mesh_ || normalized_positions_.empty() || unique_edges_.empty()) {
        result.unavailable_reason = "No mesh is available for edge loop selection.";
        return result;
    }

    if (selected_edge_indices_.empty()) {
        result.unavailable_reason = "Select an edge first.";
        edge_loop_cycle_ = {};
        return result;
    }

    const auto edge_has_topology_vertex = [this](std::uint32_t edge_index, std::uint32_t topology_vertex) {
        if (edge_index >= unique_edge_topology_vertices_.size()) {
            return false;
        }

        const Edge& topology_edge = unique_edge_topology_vertices_[edge_index];
        return topology_edge.a == topology_vertex || topology_edge.b == topology_vertex;
    };

    const auto other_topology_vertex = [this](std::uint32_t edge_index, std::uint32_t topology_vertex) {
        const Edge& topology_edge = unique_edge_topology_vertices_[edge_index];
        return topology_edge.a == topology_vertex ? topology_edge.b : topology_edge.a;
    };

    const auto vertex_position_for_topology_vertex =
        [this](std::uint32_t edge_index, std::uint32_t topology_vertex) -> mesh::Vec3 {
        const Edge& topology_edge = unique_edge_topology_vertices_[edge_index];
        const Edge& geometry_edge = unique_edges_[edge_index];
        return topology_edge.a == topology_vertex
            ? normalized_positions_[geometry_edge.a]
            : normalized_positions_[geometry_edge.b];
    };

    const auto boundary_edge = [this](std::uint32_t edge_index) {
        return edge_index < edge_face_indices_.size() && edge_face_indices_[edge_index].size() == 1U;
    };

    ensureExpandSelectionFaceAnalysis();
    const std::vector<FaceAnalysis> faces = buildFaceAnalysis(
        expand_selection_face_analysis_cache_.centroids,
        expand_selection_face_analysis_cache_.normals,
        expand_selection_face_analysis_cache_.plane_offsets
    );
    const float ridge_dot_limit = std::cos(kEdgeLoopRidgeMinAngleDegrees * (3.14159265358979323846F / 180.0F));
    const auto ridge_edge = [this, &faces, ridge_dot_limit](std::uint32_t edge_index) {
        if (edge_index >= edge_face_indices_.size()) {
            return false;
        }

        const std::vector<std::uint32_t>& incident_faces = edge_face_indices_[edge_index];
        if (incident_faces.size() != 2U ||
            incident_faces[0] >= faces.size() ||
            incident_faces[1] >= faces.size()) {
            return false;
        }

        const float normal_dot = std::abs(detail::dot(faces[incident_faces[0]].normal, faces[incident_faces[1]].normal));
        return normal_dot < ridge_dot_limit;
    };

    const auto add_candidate =
        [&result, this](std::vector<std::uint32_t> edge_indices, std::string label) {
            normalizeSelectionIndices(edge_indices, unique_edges_.size());
            if (edge_indices.size() <= 1U) {
                return;
            }

            for (const std::vector<std::uint32_t>& existing : edge_loop_cycle_.candidates) {
                if (indexVectorsEqual(existing, edge_indices)) {
                    return;
                }
            }

            edge_loop_cycle_.candidates.push_back(std::move(edge_indices));
            edge_loop_cycle_.labels.push_back(std::move(label));
            result.candidate_count = edge_loop_cycle_.candidates.size();
        };

    const auto current_candidate_matches_selection = [this]() {
        return edge_loop_cycle_.seed_edge_index.has_value() &&
            edge_loop_cycle_.selected_candidate_index < edge_loop_cycle_.candidates.size() &&
            indexVectorsEqual(
                edge_loop_cycle_.candidates[edge_loop_cycle_.selected_candidate_index],
                selected_edge_indices_
            );
    };

    if (!current_candidate_matches_selection()) {
        std::uint32_t seed_edge_index = edge_selection_anchor_.value_or(selected_edge_indices_.front());
        if (!containsIndex(selected_edge_indices_, seed_edge_index)) {
            seed_edge_index = selected_edge_indices_.front();
        }
        if (seed_edge_index >= unique_edges_.size()) {
            result.unavailable_reason = "The selected edge is no longer valid.";
            edge_loop_cycle_ = {};
            return result;
        }

        edge_loop_cycle_ = {};
        edge_loop_cycle_.seed_edge_index = seed_edge_index;

        if (boundary_edge(seed_edge_index)) {
            std::vector<std::uint32_t> boundary_component;
            std::vector<unsigned char> visited(unique_edges_.size(), 0);
            std::deque<std::uint32_t> frontier{seed_edge_index};
            visited[seed_edge_index] = 1;
            while (!frontier.empty()) {
                const std::uint32_t edge_index = frontier.front();
                frontier.pop_front();
                boundary_component.push_back(edge_index);

                if (edge_index >= edge_neighbors_.size()) {
                    continue;
                }

                for (const std::uint32_t neighbor_edge_index : edge_neighbors_[edge_index]) {
                    if (neighbor_edge_index >= unique_edges_.size() ||
                        visited[neighbor_edge_index] != 0 ||
                        !boundary_edge(neighbor_edge_index)) {
                        continue;
                    }

                    visited[neighbor_edge_index] = 1;
                    frontier.push_back(neighbor_edge_index);
                }
            }

            add_candidate(std::move(boundary_component), "Hole boundary");
        }

        if (ridge_edge(seed_edge_index) && seed_edge_index < unique_edge_topology_vertices_.size()) {
            const Edge& seed_topology_edge = unique_edge_topology_vertices_[seed_edge_index];
            std::vector<unsigned char> visited(unique_edges_.size(), 0);
            visited[seed_edge_index] = 1;

            const auto walk_ridge_direction =
                [this, &visited, &edge_has_topology_vertex, &other_topology_vertex, &vertex_position_for_topology_vertex, &ridge_edge](
                    std::uint32_t current_edge_index,
                    std::uint32_t topology_vertex
                ) {
                    std::vector<std::uint32_t> branch;
                    while (current_edge_index < edge_neighbors_.size()) {
                        const mesh::Vec3 shared_position =
                            vertex_position_for_topology_vertex(current_edge_index, topology_vertex);
                        const mesh::Vec3 previous_position =
                            vertex_position_for_topology_vertex(
                                current_edge_index,
                                other_topology_vertex(current_edge_index, topology_vertex)
                            );
                        const mesh::Vec3 incoming_direction =
                            detail::normalize(detail::subtract(shared_position, previous_position));

                        std::uint32_t best_edge_index = std::numeric_limits<std::uint32_t>::max();
                        std::uint32_t best_next_vertex = std::numeric_limits<std::uint32_t>::max();
                        float best_alignment = -std::numeric_limits<float>::infinity();

                        for (const std::uint32_t neighbor_edge_index : edge_neighbors_[current_edge_index]) {
                            if (neighbor_edge_index >= unique_edges_.size() ||
                                visited[neighbor_edge_index] != 0 ||
                                !ridge_edge(neighbor_edge_index) ||
                                !edge_has_topology_vertex(neighbor_edge_index, topology_vertex)) {
                                continue;
                            }

                            const std::uint32_t next_vertex =
                                other_topology_vertex(neighbor_edge_index, topology_vertex);
                            const mesh::Vec3 next_position =
                                vertex_position_for_topology_vertex(neighbor_edge_index, next_vertex);
                            const mesh::Vec3 outgoing_direction =
                                detail::normalize(detail::subtract(next_position, shared_position));
                            const float alignment = detail::dot(incoming_direction, outgoing_direction);
                            if (alignment > best_alignment) {
                                best_alignment = alignment;
                                best_edge_index = neighbor_edge_index;
                                best_next_vertex = next_vertex;
                            }
                        }

                        if (best_edge_index == std::numeric_limits<std::uint32_t>::max()) {
                            break;
                        }

                        visited[best_edge_index] = 1;
                        branch.push_back(best_edge_index);
                        current_edge_index = best_edge_index;
                        topology_vertex = best_next_vertex;
                    }

                    return branch;
                };

            std::vector<std::uint32_t> ridge_chain =
                walk_ridge_direction(seed_edge_index, seed_topology_edge.a);
            std::reverse(ridge_chain.begin(), ridge_chain.end());
            ridge_chain.push_back(seed_edge_index);
            std::vector<std::uint32_t> opposite_branch =
                walk_ridge_direction(seed_edge_index, seed_topology_edge.b);
            ridge_chain.insert(ridge_chain.end(), opposite_branch.begin(), opposite_branch.end());
            add_candidate(std::move(ridge_chain), "Ridge line");
        }

        const float model_diagonal = std::max(modelDiagonalLength(normalized_positions_), 0.0001F);
        const float tolerance_distance = model_diagonal * kEdgeLoopCoplanarTolerancePercent * 0.01F;
        const float normal_alignment_tolerance = std::max(kEdgeLoopCoplanarTolerancePercent * 0.01F, 1.0e-5F);
        std::size_t coplanar_candidate_count = 0;
        if (seed_edge_index < edge_face_indices_.size()) {
            for (const std::uint32_t seed_face_index : edge_face_indices_[seed_edge_index]) {
                if (seed_face_index >= faces.size()) {
                    continue;
                }

                std::vector<unsigned char> in_region(faces.size(), 0);
                std::deque<std::uint32_t> frontier{seed_face_index};
                in_region[seed_face_index] = 1;

                while (!frontier.empty()) {
                    const std::uint32_t current_face_index = frontier.front();
                    frontier.pop_front();
                    if (current_face_index >= face_neighbors_.size()) {
                        continue;
                    }

                    for (const std::uint32_t neighbor_face_index : face_neighbors_[current_face_index]) {
                        if (neighbor_face_index >= faces.size() || in_region[neighbor_face_index] != 0) {
                            continue;
                        }

                        if (!facesMatchCoplanar(
                                faces[neighbor_face_index],
                                faces[seed_face_index],
                                tolerance_distance,
                                normal_alignment_tolerance,
                                false
                            )) {
                            continue;
                        }

                        in_region[neighbor_face_index] = 1;
                        frontier.push_back(neighbor_face_index);
                    }
                }

                std::vector<std::uint32_t> boundary_edges;
                boundary_edges.reserve(unique_edges_.size());
                for (std::size_t edge_index = 0; edge_index < edge_face_indices_.size(); ++edge_index) {
                    const std::vector<std::uint32_t>& incident_faces = edge_face_indices_[edge_index];
                    std::size_t in_region_count = 0;
                    for (const std::uint32_t face_index : incident_faces) {
                        if (face_index < in_region.size() && in_region[face_index] != 0) {
                            ++in_region_count;
                        }
                    }

                    if (in_region_count > 0U &&
                        (incident_faces.size() == 1U || in_region_count < incident_faces.size())) {
                        boundary_edges.push_back(static_cast<std::uint32_t>(edge_index));
                    }
                }

                const std::size_t candidate_count_before = edge_loop_cycle_.candidates.size();
                add_candidate(
                    std::move(boundary_edges),
                    "Coplanar boundary " + std::to_string(coplanar_candidate_count + 1U)
                );
                if (edge_loop_cycle_.candidates.size() > candidate_count_before) {
                    ++coplanar_candidate_count;
                }
            }
        }

        if (coplanar_candidate_count == 1U) {
            for (std::string& label : edge_loop_cycle_.labels) {
                if (label == "Coplanar boundary 1") {
                    label = "Coplanar boundary";
                    break;
                }
            }
        }

        if (edge_loop_cycle_.candidates.empty()) {
            result.unavailable_reason = "No loop candidates were found for the selected edge.";
            edge_loop_cycle_ = {};
            return result;
        }

        edge_loop_cycle_.selected_candidate_index = 0;
    } else {
        edge_loop_cycle_.selected_candidate_index =
            (edge_loop_cycle_.selected_candidate_index + 1U) % edge_loop_cycle_.candidates.size();
    }

    const std::size_t candidate_index = edge_loop_cycle_.selected_candidate_index;
    selected_edge_indices_ = edge_loop_cycle_.candidates[candidate_index];
    edge_selection_anchor_ = edge_loop_cycle_.seed_edge_index;
    selection_summary_ = SelectionSummary{
        .edge_count = selected_edge_indices_.size(),
        .face_count = selected_face_indices_.size(),
        .point_count = selected_point_indices_.size(),
    };
    updateHighlightBuffers();

    result.available = true;
    result.candidate_count = edge_loop_cycle_.candidates.size();
    result.selected_candidate_index = candidate_index;
    result.candidate_label = edge_loop_cycle_.labels[candidate_index];
    result.selection = currentSelection();
    return result;
}

SelectSimilarResult ViewportRenderer::evaluateSelectSimilar(const SelectSimilarParams& params) const {
    std::vector<SimilarityEdge> edges;
    edges.reserve(unique_edge_topology_vertices_.size());
    for (const Edge& edge : unique_edge_topology_vertices_) {
        edges.push_back(SimilarityEdge{.a = edge.a, .b = edge.b});
    }
    return render::evaluateSelectSimilar(topology_positions_, edges, currentSelection(), params);
}

void ViewportRenderer::ensureExpandSelectionFaceAnalysis() const {
    if (!has_document_mesh_) {
        expand_selection_face_analysis_cache_ = {};
        return;
    }

    if (expand_selection_face_analysis_cache_.mesh_revision == uploaded_mesh_state_.mesh_revision &&
        expand_selection_face_analysis_cache_.triangle_count == normalized_triangles_.size() &&
        expand_selection_face_analysis_cache_.centroids.size() == normalized_triangles_.size() &&
        expand_selection_face_analysis_cache_.normals.size() == normalized_triangles_.size() &&
        expand_selection_face_analysis_cache_.plane_offsets.size() == normalized_triangles_.size()) {
        return;
    }

    expand_selection_face_analysis_cache_.mesh_revision = uploaded_mesh_state_.mesh_revision;
    expand_selection_face_analysis_cache_.triangle_count = normalized_triangles_.size();
    expand_selection_face_analysis_cache_.centroids.clear();
    expand_selection_face_analysis_cache_.normals.clear();
    expand_selection_face_analysis_cache_.plane_offsets.clear();
    expand_selection_face_analysis_cache_.centroids.reserve(normalized_triangles_.size());
    expand_selection_face_analysis_cache_.normals.reserve(normalized_triangles_.size());
    expand_selection_face_analysis_cache_.plane_offsets.reserve(normalized_triangles_.size());

    for (const mesh::Triangle& triangle : normalized_triangles_) {
        const mesh::Vec3 centroid = detail::scale(
            detail::add(detail::add(normalized_positions_[triangle.a], normalized_positions_[triangle.b]), normalized_positions_[triangle.c]),
            1.0F / 3.0F
        );
        const mesh::Vec3 edge_ab = detail::subtract(normalized_positions_[triangle.b], normalized_positions_[triangle.a]);
        const mesh::Vec3 edge_ac = detail::subtract(normalized_positions_[triangle.c], normalized_positions_[triangle.a]);
        const mesh::Vec3 normal = detail::normalize(detail::cross(edge_ab, edge_ac));
        expand_selection_face_analysis_cache_.centroids.push_back(centroid);
        expand_selection_face_analysis_cache_.normals.push_back(normal);
        expand_selection_face_analysis_cache_.plane_offsets.push_back(detail::dot(normal, centroid));
    }
}

ViewportRenderer::ExpandSelectionResult ViewportRenderer::evaluateExpandSelection(const ExpandSelectionParams& params) const {
    ExpandSelectionResult result;
    result.selection = currentSelection();

    if (!has_document_mesh_ || normalized_positions_.empty() || normalized_triangles_.empty()) {
        result.unavailable_reasons.push_back("No mesh is available for expansion.");
        return result;
    }

    if (selected_face_indices_.empty()) {
        result.unavailable_reasons.push_back("Select at least one face.");
        return result;
    }

    ensureExpandSelectionFaceAnalysis();
    const std::vector<FaceAnalysis> faces = buildFaceAnalysis(
        expand_selection_face_analysis_cache_.centroids,
        expand_selection_face_analysis_cache_.normals,
        expand_selection_face_analysis_cache_.plane_offsets
    );
    const float model_diagonal = std::max(modelDiagonalLength(normalized_positions_), 0.0001F);

    switch (params.method) {
        case ExpandSelectionMethod::Coplanar: {
            const float tolerance_distance =
                model_diagonal * std::max(params.coplanar_tolerance_percent, 0.0F) * 0.01F;
            const float normal_alignment_tolerance =
                std::max(params.coplanar_tolerance_percent * 0.01F, 1.0e-5F);
            std::vector<std::uint32_t> expanded_faces = selected_face_indices_;
            std::vector<unsigned char> expanded_face_mask(faces.size(), 0);
            for (const std::uint32_t face_index : expanded_faces) {
                if (face_index < expanded_face_mask.size()) {
                    expanded_face_mask[face_index] = 1;
                }
            }
            if (params.coplanar_select_adjacent_only) {
                std::deque<std::uint32_t> frontier(selected_face_indices_.begin(), selected_face_indices_.end());
                while (!frontier.empty()) {
                    const std::uint32_t current_face_index = frontier.front();
                    frontier.pop_front();
                    if (current_face_index >= face_neighbors_.size() || current_face_index >= faces.size()) {
                        continue;
                    }

                    for (const std::uint32_t neighbor_face_index : face_neighbors_[current_face_index]) {
                        if (neighbor_face_index >= faces.size() || expanded_face_mask[neighbor_face_index] != 0) {
                            continue;
                        }

                        if (!facesMatchCoplanar(
                                faces[neighbor_face_index],
                                faces[current_face_index],
                                tolerance_distance,
                                normal_alignment_tolerance,
                                params.coplanar_include_parallel
                            )) {
                            continue;
                        }

                        expanded_face_mask[neighbor_face_index] = 1;
                        expanded_faces.push_back(neighbor_face_index);
                        frontier.push_back(neighbor_face_index);
                    }
                }
            } else {
                for (std::size_t face_index = 0; face_index < faces.size(); ++face_index) {
                    if (expanded_face_mask[face_index] != 0) {
                        continue;
                    }
                    for (const std::uint32_t selected_face_index : selected_face_indices_) {
                        if (selected_face_index >= faces.size()) {
                            continue;
                        }

                        if (!facesMatchCoplanar(
                                faces[face_index],
                                faces[selected_face_index],
                                tolerance_distance,
                                normal_alignment_tolerance,
                                params.coplanar_include_parallel
                            )) {
                            continue;
                        }

                        expanded_face_mask[face_index] = 1;
                        expanded_faces.push_back(static_cast<std::uint32_t>(face_index));
                        break;
                    }
                }
            }

            result.available = true;
            result.selection = makeExpandedFaceSelection(currentSelection(), std::move(expanded_faces), normalized_triangles_.size());
            result.preview_face_indices = collectPreviewFaces(selected_face_indices_, result.selection.face_indices);
            return result;
        }
        case ExpandSelectionMethod::Adjacent: {
            const float max_angle_radians =
                std::max(params.adjacent_max_angle_degrees, 0.0F) * (3.14159265358979323846F / 180.0F);
            const float min_dot = std::cos(max_angle_radians);

            std::vector<std::uint32_t> expanded_faces = selected_face_indices_;
            std::vector<unsigned char> expanded_face_mask(faces.size(), 0);
            for (const std::uint32_t face_index : expanded_faces) {
                if (face_index < expanded_face_mask.size()) {
                    expanded_face_mask[face_index] = 1;
                }
            }
            std::deque<std::uint32_t> frontier(selected_face_indices_.begin(), selected_face_indices_.end());
            while (!frontier.empty()) {
                const std::uint32_t current_face_index = frontier.front();
                frontier.pop_front();
                if (current_face_index >= face_neighbors_.size() || current_face_index >= faces.size()) {
                    continue;
                }

                for (const std::uint32_t neighbor_face_index : face_neighbors_[current_face_index]) {
                    if (neighbor_face_index >= faces.size() || expanded_face_mask[neighbor_face_index] != 0) {
                        continue;
                    }

                    const float normal_dot = detail::dot(faces[current_face_index].normal, faces[neighbor_face_index].normal);
                    if (normal_dot < min_dot) {
                        continue;
                    }

                    expanded_face_mask[neighbor_face_index] = 1;
                    expanded_faces.push_back(neighbor_face_index);
                    frontier.push_back(neighbor_face_index);
                }
            }

            result.available = true;
            result.selection = makeExpandedFaceSelection(currentSelection(), std::move(expanded_faces), normalized_triangles_.size());
            result.preview_face_indices = collectPreviewFaces(selected_face_indices_, result.selection.face_indices);
            return result;
        }
        case ExpandSelectionMethod::IntersectingNormals: {
            if (selected_face_indices_.size() < 2U) {
                result.unavailable_reasons.push_back("Select at least 2 faces to define a target.");
                return result;
            }

            const float tolerance = std::max(params.intersecting_tolerance, 0.0001F);
            const float max_target_distance = model_diagonal * 10.0F;
            const PointTarget point_target = findCommonPointTarget(
                faces,
                selected_face_indices_,
                tolerance,
                params.intersecting_include_inverse_normals,
                max_target_distance
            );

            result.linear_intersection_enabled =
                selected_face_indices_.size() >= 4U && !point_target.valid;

            if (point_target.valid && params.intersecting_allow_linear_intersection) {
                result.unavailable_reasons.push_back("Linear intersection is only available when no common point target exists.");
                return result;
            }

            if (point_target.valid) {
                std::vector<std::uint32_t> expanded_faces = selected_face_indices_;
                std::vector<unsigned char> expanded_face_mask(faces.size(), 0);
                for (const std::uint32_t face_index : expanded_faces) {
                    if (face_index < expanded_face_mask.size()) {
                        expanded_face_mask[face_index] = 1;
                    }
                }
                for (std::size_t face_index = 0; face_index < faces.size(); ++face_index) {
                    if (expanded_face_mask[face_index] != 0) {
                        continue;
                    }
                    if (pointMatchesNormalTarget(
                            faces[face_index],
                            point_target.point,
                            tolerance,
                            params.intersecting_include_inverse_normals
                        )) {
                        expanded_face_mask[face_index] = 1;
                        expanded_faces.push_back(static_cast<std::uint32_t>(face_index));
                    }
                }

                result.available = true;
                result.selection = makeExpandedFaceSelection(currentSelection(), std::move(expanded_faces), normalized_triangles_.size());
                result.preview_face_indices = collectPreviewFaces(selected_face_indices_, result.selection.face_indices);
                return result;
            }

            if (point_target.too_far) {
                result.unavailable_reasons.push_back("The shared target is farther than 10x the part bounds.");
            } else {
                result.unavailable_reasons.push_back("Selected face normals do not share a common target within tolerance.");
            }

            if (!params.intersecting_allow_linear_intersection) {
                if (selected_face_indices_.size() < 4U) {
                    result.unavailable_reasons.push_back("Linear intersection requires at least 4 selected faces.");
                }
                return result;
            }

            if (selected_face_indices_.size() < 4U) {
                result.unavailable_reasons.push_back("Linear intersection requires at least 4 selected faces.");
                return result;
            }

            const LineTarget line_target = findCommonLineTarget(
                faces,
                selected_face_indices_,
                tolerance,
                params.intersecting_include_inverse_normals,
                max_target_distance
            );
            if (!line_target.valid) {
                result.unavailable_reasons.push_back("Selected face normals do not define a stable line target.");
                return result;
            }

            std::vector<std::uint32_t> expanded_faces = selected_face_indices_;
            std::vector<unsigned char> expanded_face_mask(faces.size(), 0);
            for (const std::uint32_t face_index : expanded_faces) {
                if (face_index < expanded_face_mask.size()) {
                    expanded_face_mask[face_index] = 1;
                }
            }
            for (std::size_t face_index = 0; face_index < faces.size(); ++face_index) {
                if (expanded_face_mask[face_index] != 0) {
                    continue;
                }
                if (lineMatchesNormalTarget(
                        faces[face_index],
                        line_target.point,
                        line_target.direction,
                        tolerance,
                        params.intersecting_include_inverse_normals
                    )) {
                    expanded_face_mask[face_index] = 1;
                    expanded_faces.push_back(static_cast<std::uint32_t>(face_index));
                }
            }

            result.available = true;
            result.selection = makeExpandedFaceSelection(currentSelection(), std::move(expanded_faces), normalized_triangles_.size());
            result.preview_face_indices = collectPreviewFaces(selected_face_indices_, result.selection.face_indices);
            result.unavailable_reasons.clear();
            return result;
        }
    }

    return result;
}

void ViewportRenderer::setExpandSelectionPreview(std::vector<std::uint32_t> face_indices) {
    normalizeSelectionIndices(face_indices, normalized_triangles_.size());
    preview_face_indices_ = std::move(face_indices);
    updateHighlightBuffers();
}

void ViewportRenderer::clearExpandSelectionPreview() {
    preview_face_indices_.clear();
    updateHighlightBuffers();
}

void ViewportRenderer::setSelectSimilarPreview(std::vector<std::uint32_t> edge_indices) {
    normalizeSelectionIndices(edge_indices, unique_edges_.size());
    preview_edge_indices_ = std::move(edge_indices);
    updateHighlightBuffers();
}

void ViewportRenderer::clearSelectSimilarPreview() {
    preview_edge_indices_.clear();
    updateHighlightBuffers();
}

}  // namespace meshtools::render
