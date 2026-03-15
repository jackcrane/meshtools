#include "meshtools/app/EditorDocumentState.h"

namespace meshtools::app {

EditableDocumentState makeEditableDocumentState(const mesh::MeshDocument& document) {
    return EditableDocumentState{
        .up_axis = document.up_axis,
        .mesh_revision = document.mesh_revision,
        .positions = document.positions,
        .normals = document.normals,
        .triangles = document.triangles,
        .explicit_edges = document.explicit_edges,
        .bounds = document.bounds,
        .entity_sets = document.entity_sets,
    };
}

void applyEditableDocumentState(mesh::MeshDocument* document, const EditableDocumentState& document_state) {
    if (document == nullptr) {
        return;
    }

    document->up_axis = document_state.up_axis;
    document->mesh_revision = document_state.mesh_revision;
    document->positions = document_state.positions;
    document->normals = document_state.normals;
    document->triangles = document_state.triangles;
    document->explicit_edges = document_state.explicit_edges;
    document->bounds = document_state.bounds;
    document->entity_sets = document_state.entity_sets;
}

}  // namespace meshtools::app
