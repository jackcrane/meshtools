#include "meshtools/mesh/MeshOperations/DeleteSelection.h"
#include "meshtools/mesh/MeshOperations/Detail.h"
#include "meshtools/mesh/MeshOperations/Normals.h"

#include <algorithm>

namespace meshtools::mesh {

ModifyDeleteAvailability computeModifyDeleteAvailability(const MeshDocument& document, const EntitySelection& selection) {
    ModifyDeleteAvailability availability;
    const operations::detail::MeshTopology topology = operations::detail::buildMeshTopology(document);

    for (const std::uint32_t face_index : selection.face_indices) {
        if (face_index < document.triangles.size()) {
            ++availability.face_count;
        }
    }

    for (const std::uint32_t edge_index : selection.edge_indices) {
        if (edge_index >= topology.face_indices_by_edge.size()) {
            continue;
        }

        if (topology.face_indices_by_edge[edge_index].size() <= 1U) {
            ++availability.outside_edge_count;
        } else {
            ++availability.inside_edge_count;
        }
    }

    for (const std::uint32_t point_index : selection.point_indices) {
        if (point_index < document.positions.size()) {
            ++availability.point_count;
        }
    }

    return availability;
}

ModifyDeleteResult applyModifyDelete(
    MeshDocument* document,
    const EntitySelection& selection,
    const ModifyDeleteOptions& options
) {
    ModifyDeleteResult result;
    if (document == nullptr || !options.any()) {
        return result;
    }

    ensureRenderableNormals(document);

    const operations::detail::MeshTopology old_topology = operations::detail::buildMeshTopology(*document);
    std::vector<bool> faces_to_delete(document->triangles.size(), false);
    std::vector<bool> points_to_delete(document->positions.size(), false);
    std::vector<bool> explicit_edges_to_delete(document->explicit_edges.size(), false);

    if (options.faces) {
        for (const std::uint32_t face_index : selection.face_indices) {
            if (face_index < faces_to_delete.size() && !faces_to_delete[face_index]) {
                faces_to_delete[face_index] = true;
                ++result.deleted_selection.face_count;
            }
        }
    }

    if (options.inside_edges || options.outside_edges) {
        for (const std::uint32_t edge_index : selection.edge_indices) {
            if (edge_index >= old_topology.face_indices_by_edge.size()) {
                continue;
            }

            const bool is_outside_edge = old_topology.face_indices_by_edge[edge_index].size() <= 1U;
            if (is_outside_edge) {
                if (!options.outside_edges) {
                    continue;
                }
                ++result.deleted_selection.outside_edge_count;
            } else {
                if (!options.inside_edges) {
                    continue;
                }
                ++result.deleted_selection.inside_edge_count;
            }

            for (const std::uint32_t face_index : old_topology.face_indices_by_edge[edge_index]) {
                if (face_index < faces_to_delete.size()) {
                    faces_to_delete[face_index] = true;
                }
            }
            if (edge_index < old_topology.edge_is_explicit.size() &&
                old_topology.edge_is_explicit[edge_index] &&
                edge_index < old_topology.explicit_edge_document_indices.size()) {
                const std::uint32_t explicit_edge_index = old_topology.explicit_edge_document_indices[edge_index];
                if (explicit_edge_index < explicit_edges_to_delete.size()) {
                    explicit_edges_to_delete[explicit_edge_index] = true;
                }
            }
        }
    }

    if (options.points) {
        for (const std::uint32_t point_index : selection.point_indices) {
            if (point_index >= points_to_delete.size() || points_to_delete[point_index]) {
                continue;
            }

            points_to_delete[point_index] = true;
            ++result.deleted_selection.point_count;
            for (const std::uint32_t face_index : old_topology.face_indices_by_point[point_index]) {
                if (face_index < faces_to_delete.size()) {
                    faces_to_delete[face_index] = true;
                }
            }
        }
    }

    result.deleted_face_count = static_cast<std::size_t>(std::count(faces_to_delete.begin(), faces_to_delete.end(), true));
    result.deleted_edge_count =
        static_cast<std::size_t>(std::count(explicit_edges_to_delete.begin(), explicit_edges_to_delete.end(), true));
    result.deleted_point_count = static_cast<std::size_t>(std::count(points_to_delete.begin(), points_to_delete.end(), true));
    result.changed =
        result.deleted_face_count > 0 ||
        result.deleted_edge_count > 0 ||
        result.deleted_point_count > 0;
    if (!result.changed) {
        return result;
    }

    std::vector<std::uint32_t> old_to_new_face(document->triangles.size(), operations::detail::kInvalidIndex);
    std::vector<Triangle> new_triangles;
    new_triangles.reserve(document->triangles.size() - result.deleted_face_count);
    for (std::size_t face_index = 0; face_index < document->triangles.size(); ++face_index) {
        if (faces_to_delete[face_index]) {
            continue;
        }

        old_to_new_face[face_index] = static_cast<std::uint32_t>(new_triangles.size());
        new_triangles.push_back(document->triangles[face_index]);
    }

    std::vector<std::uint32_t> old_to_new_point(document->positions.size(), operations::detail::kInvalidIndex);
    std::vector<Vec3> new_positions;
    new_positions.reserve(document->positions.size() - result.deleted_point_count);
    std::vector<Vec3> new_normals;
    new_normals.reserve(document->positions.size() - result.deleted_point_count);
    for (std::size_t point_index = 0; point_index < document->positions.size(); ++point_index) {
        if (points_to_delete[point_index]) {
            continue;
        }

        old_to_new_point[point_index] = static_cast<std::uint32_t>(new_positions.size());
        new_positions.push_back(document->positions[point_index]);
        if (point_index < document->normals.size()) {
            new_normals.push_back(document->normals[point_index]);
        }
    }

    for (Triangle& triangle : new_triangles) {
        triangle.a = triangle.a < old_to_new_point.size() ? old_to_new_point[triangle.a] : operations::detail::kInvalidIndex;
        triangle.b = triangle.b < old_to_new_point.size() ? old_to_new_point[triangle.b] : operations::detail::kInvalidIndex;
        triangle.c = triangle.c < old_to_new_point.size() ? old_to_new_point[triangle.c] : operations::detail::kInvalidIndex;
    }
    new_triangles.erase(
        std::remove_if(new_triangles.begin(), new_triangles.end(), [](const Triangle& triangle) {
            return triangle.a == operations::detail::kInvalidIndex ||
                   triangle.b == operations::detail::kInvalidIndex ||
                   triangle.c == operations::detail::kInvalidIndex;
        }),
        new_triangles.end()
    );

    document->positions = std::move(new_positions);
    document->triangles = std::move(new_triangles);
    if (!document->explicit_edges.empty()) {
        std::vector<EdgeSegment> new_explicit_edges;
        new_explicit_edges.reserve(document->explicit_edges.size());
        for (std::size_t explicit_edge_index = 0; explicit_edge_index < document->explicit_edges.size(); ++explicit_edge_index) {
            if (explicit_edge_index < explicit_edges_to_delete.size() && explicit_edges_to_delete[explicit_edge_index]) {
                continue;
            }
            new_explicit_edges.push_back(document->explicit_edges[explicit_edge_index]);
        }
        document->explicit_edges = std::move(new_explicit_edges);
    }
    operations::detail::remapExplicitEdges(&document->explicit_edges, old_to_new_point);
    document->bounds = operations::detail::calculateBounds(document->positions);
    if (new_normals.size() == document->positions.size()) {
        document->normals = std::move(new_normals);
    } else {
        document->normals = operations::detail::calculateVertexNormals(document->positions, document->triangles);
    }

    const operations::detail::MeshTopology new_topology = operations::detail::buildMeshTopology(*document);
    for (EntitySet& entity_set : document->entity_sets) {
        operations::detail::remapIndexVector(&entity_set.members.face_indices, old_to_new_face);
        operations::detail::remapIndexVector(&entity_set.members.point_indices, old_to_new_point);
        operations::detail::remapEdgeIndexVector(&entity_set.members.edge_indices, old_topology, new_topology);
    }

    return result;
}

}  // namespace meshtools::mesh
