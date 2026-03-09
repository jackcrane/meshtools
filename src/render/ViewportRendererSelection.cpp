#include "meshtools/render/ViewportRenderer.h"

#include <algorithm>
#include <deque>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#include "ViewportRendererDetail.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#endif

namespace meshtools::render {
namespace {

bool containsIndex(const std::vector<std::uint32_t>& indices, std::uint32_t index) {
    return std::find(indices.begin(), indices.end(), index) != indices.end();
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
    updateHighlightBuffers();
    return selection_summary_.totalCount();
}

void ViewportRenderer::clearSelection() {
    selected_edge_indices_.clear();
    selected_face_indices_.clear();
    selected_point_indices_.clear();
    face_selection_anchor_.reset();
    edge_selection_anchor_.reset();
    selection_summary_ = SelectionSummary{};
    updateHighlightBuffers();
}

}  // namespace meshtools::render
