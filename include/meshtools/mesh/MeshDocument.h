#pragma once

#include <cstddef>
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

struct EdgeSegment {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
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

struct MeshDocument {
    std::filesystem::path source_path;
    std::string display_name_override;
    MeshFormat format = MeshFormat::Obj;
    UpAxis up_axis = UpAxis::Y;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Triangle> triangles;
    std::vector<EdgeSegment> explicit_edges;
    Bounds bounds;
    std::vector<EntitySet> entity_sets;

    [[nodiscard]] std::string displayName() const;
    [[nodiscard]] std::string formatLabel() const;
};

}  // namespace meshtools::mesh
