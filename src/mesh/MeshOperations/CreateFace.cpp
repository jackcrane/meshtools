#include "meshtools/mesh/MeshOperations/CreateFace.h"
#include "meshtools/mesh/MeshOperations/Detail.h"
#include "meshtools/mesh/MeshOperations/Normals.h"

#include <algorithm>

namespace meshtools::mesh {

ModifyCreateFaceAvailability computeModifyCreateFaceAvailability(
    const MeshDocument& document,
    const EntitySelection& selection
) {
    const operations::detail::MeshTopology topology = operations::detail::buildMeshTopology(document);
    return computeModifyCreateFaceAvailability(document, topology, selection);
}

ModifyCreateFaceAvailability computeModifyCreateFaceAvailability(
    const MeshDocument& document,
    const operations::detail::MeshTopology& topology,
    const EntitySelection& selection
) {
    ModifyCreateFaceAvailability availability;
    for (const std::uint32_t point_index : selection.point_indices) {
        if (point_index < document.positions.size()) {
            ++availability.point_count;
        }
    }

    for (const std::uint32_t edge_index : selection.edge_indices) {
        if (edge_index < topology.edge_points.size()) {
            ++availability.edge_count;
        }
    }

    availability.candidate_point_count = operations::detail::collectPointsFromSelectedEdges(topology, selection).size();
    return availability;
}

ModifyCreateFaceResult applyModifyCreateFace(MeshDocument* document, const EntitySelection& selection) {
    ModifyCreateFaceResult result;
    if (document == nullptr) {
        return result;
    }

    ensureRenderableNormals(document);

    const operations::detail::MeshTopology topology = operations::detail::buildMeshTopology(*document);
    const ModifyCreateFaceAvailability availability = computeModifyCreateFaceAvailability(*document, selection);
    const Vec3 point_reference_normal =
        operations::detail::computeReferenceNormal(*document, topology, selection, true);
    const Vec3 edge_reference_normal =
        operations::detail::computeReferenceNormal(*document, topology, selection, false);
    std::vector<std::uint32_t> ordered_points;
    bool used_point_selection = false;
    if (availability.point_count >= 3) {
        ordered_points =
            operations::detail::sortPointsForFace(document->positions, selection.point_indices, point_reference_normal);
        used_point_selection = ordered_points.size() >= 3;
    }
    if (ordered_points.size() < 3 && availability.edge_count >= 2) {
        ordered_points =
            operations::detail::orderPointsFromEdges(topology, document->positions, selection, edge_reference_normal);
        used_point_selection = false;
    }

    if (ordered_points.size() >= 3) {
        const Vec3 polygon_normal = operations::detail::computePolygonNormal(document->positions, ordered_points);
        const Vec3 reference_normal = used_point_selection ? point_reference_normal : edge_reference_normal;
        if (operations::detail::length(polygon_normal) > 1.0e-6F &&
            operations::detail::length(reference_normal) > 1.0e-6F &&
            operations::detail::dot(polygon_normal, reference_normal) < 0.0F) {
            std::reverse(ordered_points.begin(), ordered_points.end());
        }
    }

    const Vec3 triangulation_normal = used_point_selection ? point_reference_normal : edge_reference_normal;
    const std::vector<Triangle> created_triangles =
        operations::detail::triangulateOrderedFace(document->positions, ordered_points, triangulation_normal);
    if (created_triangles.empty()) {
        return result;
    }

    const std::uint32_t first_face_index = static_cast<std::uint32_t>(document->triangles.size());
    document->triangles.insert(document->triangles.end(), created_triangles.begin(), created_triangles.end());

    result.changed = true;
    result.created_face_count = created_triangles.size();
    result.created_face_indices.reserve(created_triangles.size());
    for (std::uint32_t index = 0; index < result.created_face_count; ++index) {
        result.created_face_indices.push_back(first_face_index + index);
    }

    return result;
}

}  // namespace meshtools::mesh
