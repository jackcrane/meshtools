#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "meshtools/mesh/MeshDocument.h"

namespace meshtools::app {

struct EditableDocumentState {
    mesh::UpAxis up_axis = mesh::UpAxis::Y;
    std::uint64_t mesh_revision = 0;
    std::vector<mesh::Vec3> positions;
    std::vector<mesh::Vec3> normals;
    std::vector<mesh::Triangle> triangles;
    std::vector<mesh::EdgeSegment> explicit_edges;
    mesh::Bounds bounds;
    std::vector<mesh::EntitySet> entity_sets;
};

EditableDocumentState makeEditableDocumentState(const mesh::MeshDocument& document);
void applyEditableDocumentState(mesh::MeshDocument* document, const EditableDocumentState& document_state);

}  // namespace meshtools::app
