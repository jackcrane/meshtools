#include "meshtools/mesh/MeshOperations/CreateFace.h"
#include "meshtools/mesh/MeshOperations/Detail.h"
#include "meshtools/mesh/MeshOperations/Normals.h"

#include <algorithm>
#include <iostream>
#include <queue>
#include <sstream>
#include <unordered_map>

namespace meshtools::mesh {

namespace {

std::string formatIndices(const std::vector<std::uint32_t>& indices) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < indices.size(); ++index) {
        if (index > 0) {
            stream << ", ";
        }
        stream << indices[index];
    }
    stream << ']';
    return stream.str();
}

std::string formatVec3(const Vec3& value) {
    std::ostringstream stream;
    stream << '(' << value.x << ", " << value.y << ", " << value.z << ')';
    return stream.str();
}

void logCreateFaceDebug(const std::string& message) {
    std::cout << "[FACEDBG] " << message << std::endl;
}

std::vector<std::uint32_t> validSelectedEdgeIndices(
    const operations::detail::MeshTopology& topology,
    const EntitySelection& selection
) {
    std::vector<std::uint32_t> edge_indices;
    edge_indices.reserve(selection.edge_indices.size());
    for (const std::uint32_t edge_index : selection.edge_indices) {
        if (edge_index < topology.edge_points.size()) {
            edge_indices.push_back(edge_index);
        }
    }

    operations::detail::sortAndUnique(edge_indices);
    return edge_indices;
}

std::vector<EntitySelection> splitSelectedEdgeComponents(
    const operations::detail::MeshTopology& topology,
    const EntitySelection& selection
) {
    const std::vector<std::uint32_t> edge_indices = validSelectedEdgeIndices(topology, selection);
    if (edge_indices.empty()) {
        return {};
    }

    std::unordered_map<
        operations::detail::QuantizedPositionKey,
        std::vector<std::size_t>,
        operations::detail::QuantizedPositionKeyHash> edges_by_point;
    edges_by_point.reserve(edge_indices.size() * 2ULL);
    for (std::size_t local_edge_index = 0; local_edge_index < edge_indices.size(); ++local_edge_index) {
        const std::uint32_t edge_index = edge_indices[local_edge_index];
        if (edge_index >= topology.unique_edge_keys.size()) {
            continue;
        }

        const operations::detail::QuantizedEdgeKey& edge_key = topology.unique_edge_keys[edge_index];
        edges_by_point[edge_key.a].push_back(local_edge_index);
        edges_by_point[edge_key.b].push_back(local_edge_index);
    }

    std::vector<EntitySelection> components;
    std::vector<bool> visited(edge_indices.size(), false);
    for (std::size_t local_edge_index = 0; local_edge_index < edge_indices.size(); ++local_edge_index) {
        if (visited[local_edge_index]) {
            continue;
        }

        EntitySelection component;
        std::queue<std::size_t> pending;
        pending.push(local_edge_index);
        visited[local_edge_index] = true;

        while (!pending.empty()) {
            const std::size_t current_edge_index = pending.front();
            pending.pop();

            const std::uint32_t edge_index = edge_indices[current_edge_index];
            component.edge_indices.push_back(edge_index);

            if (edge_index >= topology.unique_edge_keys.size()) {
                continue;
            }

            const operations::detail::QuantizedEdgeKey& edge_key = topology.unique_edge_keys[edge_index];
            const operations::detail::QuantizedPositionKey incident_points[2] = {edge_key.a, edge_key.b};
            for (const operations::detail::QuantizedPositionKey& point_key : incident_points) {
                const auto incident_edges_it = edges_by_point.find(point_key);
                if (incident_edges_it == edges_by_point.end()) {
                    continue;
                }

                for (const std::size_t neighbor_edge_index : incident_edges_it->second) {
                    if (visited[neighbor_edge_index]) {
                        continue;
                    }

                    visited[neighbor_edge_index] = true;
                    pending.push(neighbor_edge_index);
                }
            }
        }

        components.push_back(std::move(component));
    }

    return components;
}

std::vector<Triangle> createFaceTrianglesForSelection(
    const MeshDocument& document,
    const operations::detail::MeshTopology& topology,
    const EntitySelection& selection
) {
    const ModifyCreateFaceAvailability availability = computeModifyCreateFaceAvailability(document, topology, selection);
    const Vec3 point_reference_normal =
        operations::detail::computeReferenceNormal(document, topology, selection, true);
    const Vec3 edge_reference_normal =
        operations::detail::computeReferenceNormal(document, topology, selection, false);
    logCreateFaceDebug(
        "component selection point_indices=" + formatIndices(selection.point_indices) +
        " edge_indices=" + formatIndices(selection.edge_indices) +
        " availability(point_count=" + std::to_string(availability.point_count) +
        ", edge_count=" + std::to_string(availability.edge_count) +
        ", candidate_point_count=" + std::to_string(availability.candidate_point_count) + ")"
    );
    logCreateFaceDebug(
        "component reference_normals point=" + formatVec3(point_reference_normal) +
        " edge=" + formatVec3(edge_reference_normal)
    );

    std::vector<std::uint32_t> ordered_points;
    bool used_point_selection = false;
    if (availability.point_count >= 3) {
        ordered_points = operations::detail::orderPointsFromConnectedPoints(
            topology,
            document.positions,
            selection.point_indices,
            point_reference_normal
        );
        logCreateFaceDebug("point ordering result count=" + std::to_string(ordered_points.size()) +
                           " ordered_points=" + formatIndices(ordered_points));
        used_point_selection = ordered_points.size() >= 3;
    }
    if (ordered_points.size() < 3 && availability.edge_count >= 2) {
        ordered_points =
            operations::detail::orderPointsFromEdges(topology, document.positions, selection, edge_reference_normal);
        logCreateFaceDebug("edge ordering result count=" + std::to_string(ordered_points.size()) +
                           " ordered_points=" + formatIndices(ordered_points));
        used_point_selection = false;
    }

    if (ordered_points.size() >= 3) {
        const Vec3 polygon_normal = operations::detail::computePolygonNormal(document.positions, ordered_points);
        logCreateFaceDebug(
            "polygon_normal=" + formatVec3(polygon_normal) +
            " triangulation_source=" + std::string(used_point_selection ? "points" : "edges")
        );
        const Vec3 reference_normal = used_point_selection ? point_reference_normal : edge_reference_normal;
        if (operations::detail::length(polygon_normal) > 1.0e-6F &&
            operations::detail::length(reference_normal) > 1.0e-6F &&
            operations::detail::dot(polygon_normal, reference_normal) < 0.0F) {
            std::reverse(ordered_points.begin(), ordered_points.end());
            logCreateFaceDebug("reversed ordered_points to match reference normal; ordered_points=" +
                               formatIndices(ordered_points));
        }
    }

    const Vec3 triangulation_normal = used_point_selection ? point_reference_normal : edge_reference_normal;
    return operations::detail::triangulateOrderedFace(document.positions, ordered_points, triangulation_normal);
}

}  // namespace

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
        logCreateFaceDebug("applyModifyCreateFace aborted: document was null");
        return result;
    }

    ensureRenderableNormals(document);

    const operations::detail::MeshTopology topology = operations::detail::buildMeshTopology(*document);
    const ModifyCreateFaceAvailability availability = computeModifyCreateFaceAvailability(*document, topology, selection);
    logCreateFaceDebug(
        "start positions=" + std::to_string(document->positions.size()) +
        " triangles=" + std::to_string(document->triangles.size()) +
        " selected_points=" + std::to_string(selection.point_indices.size()) +
        " selected_edges=" + std::to_string(selection.edge_indices.size()) +
        " point_indices=" + formatIndices(selection.point_indices) +
        " edge_indices=" + formatIndices(selection.edge_indices) +
        " availability(point_count=" + std::to_string(availability.point_count) +
        ", edge_count=" + std::to_string(availability.edge_count) +
        ", candidate_point_count=" + std::to_string(availability.candidate_point_count) + ")"
    );
    std::vector<Triangle> created_triangles;
    if (availability.point_count >= 3) {
        created_triangles = createFaceTrianglesForSelection(*document, topology, selection);
    } else {
        const std::vector<EntitySelection> edge_components = splitSelectedEdgeComponents(topology, selection);
        logCreateFaceDebug("edge component count=" + std::to_string(edge_components.size()));
        for (const EntitySelection& component_selection : edge_components) {
            std::vector<Triangle> component_triangles =
                createFaceTrianglesForSelection(*document, topology, component_selection);
            if (component_triangles.empty()) {
                continue;
            }

            created_triangles.insert(
                created_triangles.end(),
                component_triangles.begin(),
                component_triangles.end()
            );
        }
    }

    if (created_triangles.empty()) {
        logCreateFaceDebug("createFaceTrianglesForSelection returned 0 triangles");
        return result;
    }

    const std::uint32_t first_face_index = static_cast<std::uint32_t>(document->triangles.size());
    document->triangles.insert(document->triangles.end(), created_triangles.begin(), created_triangles.end());
    logCreateFaceDebug(
        "success created_face_count=" + std::to_string(created_triangles.size()) +
        " first_face_index=" + std::to_string(first_face_index)
    );

    result.changed = true;
    result.created_face_count = created_triangles.size();
    result.created_face_indices.reserve(created_triangles.size());
    for (std::uint32_t index = 0; index < result.created_face_count; ++index) {
        result.created_face_indices.push_back(first_face_index + index);
    }

    return result;
}

}  // namespace meshtools::mesh
