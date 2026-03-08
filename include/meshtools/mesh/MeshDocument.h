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

struct MeshDocument {
    std::filesystem::path source_path;
    std::string display_name_override;
    MeshFormat format = MeshFormat::Obj;
    UpAxis up_axis = UpAxis::Y;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Triangle> triangles;
    Bounds bounds;

    [[nodiscard]] std::string displayName() const;
    [[nodiscard]] std::string formatLabel() const;
};

}  // namespace meshtools::mesh
