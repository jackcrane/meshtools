#include "meshtools/mesh/MeshOperations/ProjectEdge.h"
#include "meshtools/mesh/MeshOperations/Detail.h"
#include "meshtools/mesh/MeshOperations/Normals.h"

#include <algorithm>

namespace meshtools::mesh {

ModifyProjectAvailability computeModifyProjectAvailability(
    const MeshDocument& document,
    const EntitySelection& selection
) {
    ModifyProjectAvailability availability;
    const std::vector<std::uint32_t> face_indices = operations::detail::selectedFaceIndices(document, selection);
    availability.face_count = face_indices.size();
    if (face_indices.size() != 2U) {
        availability.unavailable_reason =
            "Modify Project requires exactly 2 selected faces (got " + std::to_string(face_indices.size()) + ").";
        return availability;
    }

    const operations::detail::Plane first_plane =
        operations::detail::makeTrianglePlane(document.positions, document.triangles[face_indices[0]]);
    const operations::detail::Plane second_plane =
        operations::detail::makeTrianglePlane(document.positions, document.triangles[face_indices[1]]);
    if (!first_plane.valid || !second_plane.valid) {
        availability.unavailable_reason = "Modify Project requires 2 valid non-degenerate faces.";
        return availability;
    }

    Vec3 line_point{};
    Vec3 line_direction{};
    if (!operations::detail::intersectPlanes(first_plane, second_plane, &line_point, &line_direction)) {
        availability.unavailable_reason = "Modify Project requires non-parallel face planes.";
        return availability;
    }

    (void)line_point;
    (void)line_direction;
    availability.available = true;
    return availability;
}

ModifyProjectResult applyModifyProject(MeshDocument* document, const ModifyProjectOptions& options) {
    ModifyProjectResult result;
    if (document == nullptr) {
        return result;
    }

    ensureRenderableNormals(document);

    if (options.source_face_indices[0] >= document->triangles.size() ||
        options.source_face_indices[1] >= document->triangles.size() ||
        options.source_face_indices[0] == options.source_face_indices[1]) {
        return result;
    }

    const operations::detail::Plane first_plane =
        operations::detail::planeFromFace(*document, options.source_face_indices[0]);
    const operations::detail::Plane second_plane =
        operations::detail::planeFromFace(*document, options.source_face_indices[1]);
    Vec3 line_point{};
    Vec3 line_direction{};
    if (!operations::detail::intersectPlanes(first_plane, second_plane, &line_point, &line_direction)) {
        return result;
    }

    const operations::detail::MeshTopology old_topology = operations::detail::buildMeshTopology(*document);
    const float tolerance = std::max(operations::detail::modelDiagonalLength(*document) * 1.0e-4F, 1.0e-5F);
    float start_parameter = 0.0F;
    float end_parameter = 0.0F;
    if (options.infinite_length) {
        const float extent = std::max(operations::detail::modelDiagonalLength(*document) * 4.0F, 1.0F);
        start_parameter = -extent;
        end_parameter = extent;
    } else {
        if (!options.start_target.has_value() || !options.end_target.has_value()) {
            return result;
        }

        const std::optional<float> start = operations::detail::resolveProjectTargetParameter(
            *document,
            old_topology,
            *options.start_target,
            line_point,
            line_direction,
            tolerance
        );
        const std::optional<float> end = operations::detail::resolveProjectTargetParameter(
            *document,
            old_topology,
            *options.end_target,
            line_point,
            line_direction,
            tolerance
        );
        if (!start.has_value() || !end.has_value()) {
            return result;
        }

        start_parameter = *start;
        end_parameter = *end;
    }

    if (start_parameter > end_parameter) {
        std::swap(start_parameter, end_parameter);
    }
    if (std::fabs(end_parameter - start_parameter) <= tolerance) {
        return result;
    }

    const Vec3 start_position = operations::detail::add(line_point, operations::detail::scale(line_direction, start_parameter));
    const Vec3 end_position = operations::detail::add(line_point, operations::detail::scale(line_direction, end_parameter));
    const std::uint32_t start_point_index = operations::detail::findOrAppendPointIndex(document, start_position);
    const std::uint32_t end_point_index = operations::detail::findOrAppendPointIndex(document, end_position);
    if (start_point_index == operations::detail::kInvalidIndex ||
        end_point_index == operations::detail::kInvalidIndex ||
        start_point_index == end_point_index) {
        return result;
    }

    document->explicit_edges.push_back(EdgeSegment{
        .a = start_point_index,
        .b = end_point_index,
    });
    document->bounds = operations::detail::calculateBounds(document->positions);
    document->normals = operations::detail::calculateVertexNormals(document->positions, document->triangles);

    const operations::detail::MeshTopology new_topology = operations::detail::buildMeshTopology(*document);
    const std::vector<Vec3> normalized_positions = operations::detail::normalizePositionsForTopology(*document);
    result.created_edge_index = operations::detail::findTopologyEdgeIndexForPoints(
        new_topology,
        normalized_positions,
        start_point_index,
        end_point_index
    );
    result.changed = result.created_edge_index.has_value();
    return result;
}

}  // namespace meshtools::mesh
