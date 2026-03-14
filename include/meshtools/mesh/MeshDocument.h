#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace meshtools::mesh {

enum class MeshFormat {
    Obj,
    Stl,
};

enum class UpAxis {
    Y,
    Z,
};

[[nodiscard]] const char* upAxisName(UpAxis axis);

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Triangle {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
};

struct Bounds {
    Vec3 minimum;
    Vec3 maximum;
    bool valid = false;
};

struct EntitySelection {
    std::vector<std::uint32_t> edge_indices;
    std::vector<std::uint32_t> face_indices;
    std::vector<std::uint32_t> point_indices;

    [[nodiscard]] std::size_t totalCount() const {
        return edge_indices.size() + face_indices.size() + point_indices.size();
    }

    [[nodiscard]] bool empty() const {
        return totalCount() == 0;
    }
};

struct EntitySet {
    std::string name;
    EntitySelection members;
};

struct ModifyDeleteAvailability {
    std::size_t face_count = 0;
    std::size_t inside_edge_count = 0;
    std::size_t outside_edge_count = 0;
    std::size_t point_count = 0;

    [[nodiscard]] bool any() const {
        return face_count > 0 || inside_edge_count > 0 || outside_edge_count > 0 || point_count > 0;
    }
};

struct ModifyDeleteOptions {
    bool faces = false;
    bool inside_edges = false;
    bool outside_edges = false;
    bool points = false;

    [[nodiscard]] bool any() const {
        return faces || inside_edges || outside_edges || points;
    }
};

struct ModifyDeleteResult {
    bool changed = false;
    std::size_t deleted_face_count = 0;
    std::size_t deleted_point_count = 0;
    ModifyDeleteAvailability deleted_selection;
};

struct ModifyCreateFaceAvailability {
    std::size_t point_count = 0;
    std::size_t edge_count = 0;
    std::size_t candidate_point_count = 0;

    [[nodiscard]] bool any() const {
        return point_count >= 3 || (edge_count >= 2 && candidate_point_count >= 3);
    }
};

struct ModifyCreateFaceResult {
    bool changed = false;
    std::size_t created_face_count = 0;
    std::vector<std::uint32_t> created_face_indices;
};

struct MeshDocument {
    std::filesystem::path source_path;
    std::string display_name_override;
    MeshFormat format = MeshFormat::Obj;
    UpAxis up_axis = UpAxis::Y;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Triangle> triangles;
    Bounds bounds;
    std::vector<EntitySet> entity_sets;

    [[nodiscard]] std::string displayName() const;
    [[nodiscard]] std::string formatLabel() const;
};

[[nodiscard]] ModifyDeleteAvailability computeModifyDeleteAvailability(
    const MeshDocument& document,
    const EntitySelection& selection
);
[[nodiscard]] ModifyDeleteResult applyModifyDelete(
    MeshDocument* document,
    const EntitySelection& selection,
    const ModifyDeleteOptions& options
);
[[nodiscard]] ModifyCreateFaceAvailability computeModifyCreateFaceAvailability(
    const MeshDocument& document,
    const EntitySelection& selection
);
[[nodiscard]] ModifyCreateFaceResult applyModifyCreateFace(
    MeshDocument* document,
    const EntitySelection& selection
);
void ensureRenderableNormals(MeshDocument* document);

}  // namespace meshtools::mesh
