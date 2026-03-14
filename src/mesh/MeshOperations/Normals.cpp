#include "meshtools/mesh/MeshOperations/Normals.h"

#include "meshtools/mesh/MeshOperations/Detail.h"

namespace meshtools::mesh {

void ensureRenderableNormals(MeshDocument* document) {
    if (document == nullptr) {
        return;
    }

    if (document->normals.size() == document->positions.size()) {
        return;
    }

    document->normals = operations::detail::calculateVertexNormals(document->positions, document->triangles);
}

}  // namespace meshtools::mesh
