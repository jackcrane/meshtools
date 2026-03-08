#include "meshtools/mesh/MeshDocument.h"

namespace meshtools::mesh {

const char* upAxisName(UpAxis axis) {
    switch (axis) {
        case UpAxis::Y:
            return "Y";
        case UpAxis::Z:
            return "Z";
    }

    return "Unknown";
}

std::string MeshDocument::displayName() const {
    if (source_path.filename().empty()) {
        return "Untitled";
    }

    return source_path.filename().string();
}

std::string MeshDocument::formatLabel() const {
    switch (format) {
        case MeshFormat::Obj:
            return "OBJ";
        case MeshFormat::Stl:
            return "STL";
    }

    return "Unknown";
}

}  // namespace meshtools::mesh
