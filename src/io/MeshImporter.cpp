#include "meshtools/io/MeshImporter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace meshtools::io {
namespace {

using meshtools::mesh::Bounds;
using meshtools::mesh::MeshDocument;
using meshtools::mesh::MeshFormat;
using meshtools::mesh::Triangle;
using meshtools::mesh::Vec3;

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string trim(std::string value) {
    const auto not_space = [](unsigned char character) {
        return !std::isspace(character);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

Bounds computeBounds(const std::vector<Vec3>& positions) {
    Bounds bounds;
    if (positions.empty()) {
        return bounds;
    }

    bounds.minimum = positions.front();
    bounds.maximum = positions.front();
    bounds.valid = true;

    for (const Vec3& position : positions) {
        bounds.minimum.x = std::min(bounds.minimum.x, position.x);
        bounds.minimum.y = std::min(bounds.minimum.y, position.y);
        bounds.minimum.z = std::min(bounds.minimum.z, position.z);
        bounds.maximum.x = std::max(bounds.maximum.x, position.x);
        bounds.maximum.y = std::max(bounds.maximum.y, position.y);
        bounds.maximum.z = std::max(bounds.maximum.z, position.z);
    }

    return bounds;
}

MeshImportResult failure(std::string message) {
    return MeshImportResult{.document = std::nullopt, .error_message = std::move(message)};
}

void reportProgress(
    const MeshImportProgressCallback& progress_callback,
    float progress,
    std::string_view stage
) {
    if (!progress_callback) {
        return;
    }

    progress_callback(std::clamp(progress, 0.0F, 1.0F), stage);
}

std::optional<std::size_t> resolveObjIndex(int raw_index, std::size_t vertex_count) {
    if (raw_index > 0) {
        const std::size_t zero_based = static_cast<std::size_t>(raw_index - 1);
        if (zero_based < vertex_count) {
            return zero_based;
        }
        return std::nullopt;
    }

    if (raw_index < 0) {
        const std::int64_t resolved = static_cast<std::int64_t>(vertex_count) + static_cast<std::int64_t>(raw_index);
        if (resolved >= 0 && static_cast<std::size_t>(resolved) < vertex_count) {
            return static_cast<std::size_t>(resolved);
        }
    }

    return std::nullopt;
}

MeshImportResult importObj(const std::filesystem::path& path, const MeshImportProgressCallback& progress_callback) {
    std::ifstream input(path);
    if (!input) {
        return failure("Failed to open OBJ file.");
    }
    const std::uintmax_t total_bytes = std::filesystem::file_size(path);
    reportProgress(progress_callback, 0.0F, "Reading OBJ");

    MeshDocument document;
    document.source_path = path;
    document.format = MeshFormat::Obj;

    std::string line;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;

        const std::size_t comment_offset = line.find('#');
        if (comment_offset != std::string::npos) {
            line.erase(comment_offset);
        }

        line = trim(std::move(line));
        if (line.empty()) {
            continue;
        }

        std::istringstream stream(line);
        std::string keyword;
        stream >> keyword;

        if (keyword == "v") {
            Vec3 position;
            if (!(stream >> position.x >> position.y >> position.z)) {
                return failure("Malformed OBJ vertex at line " + std::to_string(line_number) + ".");
            }
            document.positions.push_back(position);
            continue;
        }

        if (keyword == "vn") {
            Vec3 normal;
            if (!(stream >> normal.x >> normal.y >> normal.z)) {
                return failure("Malformed OBJ normal at line " + std::to_string(line_number) + ".");
            }
            document.normals.push_back(normal);
            continue;
        }

        if (keyword != "f") {
            continue;
        }

        std::vector<std::uint32_t> face_indices;
        std::string vertex_token;
        while (stream >> vertex_token) {
            const std::size_t separator = vertex_token.find('/');
            const std::string index_token = separator == std::string::npos ? vertex_token : vertex_token.substr(0, separator);

            int raw_index = 0;
            try {
                raw_index = std::stoi(index_token);
            } catch (...) {
                return failure("Malformed OBJ face index at line " + std::to_string(line_number) + ".");
            }

            const std::optional<std::size_t> resolved_index = resolveObjIndex(raw_index, document.positions.size());
            if (!resolved_index.has_value()) {
                return failure("OBJ face references an invalid vertex at line " + std::to_string(line_number) + ".");
            }

            face_indices.push_back(static_cast<std::uint32_t>(*resolved_index));
        }

        if (face_indices.size() < 3) {
            return failure("OBJ face at line " + std::to_string(line_number) + " has fewer than 3 vertices.");
        }

        for (std::size_t index = 1; index + 1 < face_indices.size(); ++index) {
            document.triangles.push_back(Triangle{
                .a = face_indices[0],
                .b = face_indices[index],
                .c = face_indices[index + 1],
            });
        }

        if ((line_number % 2048U) == 0U) {
            const std::streampos current_offset = input.tellg();
            if (current_offset > 0 && total_bytes > 0U) {
                reportProgress(
                    progress_callback,
                    static_cast<float>(current_offset) / static_cast<float>(total_bytes),
                    "Reading OBJ"
                );
            }
        }
    }

    if (document.positions.empty()) {
        return failure("OBJ file does not contain any vertices.");
    }

    document.bounds = computeBounds(document.positions);
    reportProgress(progress_callback, 1.0F, "Reading OBJ");
    return MeshImportResult{.document = std::move(document), .error_message = {}};
}

bool isLikelyBinaryStl(std::ifstream& input) {
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 84) {
        input.seekg(0, std::ios::beg);
        return false;
    }

    input.seekg(80, std::ios::beg);
    std::uint32_t triangle_count = 0;
    input.read(reinterpret_cast<char*>(&triangle_count), sizeof(triangle_count));

    const std::uint64_t expected_size = 84ULL + (static_cast<std::uint64_t>(triangle_count) * 50ULL);
    input.seekg(0, std::ios::beg);
    return expected_size == static_cast<std::uint64_t>(size);
}

MeshImportResult importBinaryStl(const std::filesystem::path& path, const MeshImportProgressCallback& progress_callback) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return failure("Failed to open STL file.");
    }
    reportProgress(progress_callback, 0.0F, "Reading binary STL");

    MeshDocument document;
    document.source_path = path;
    document.format = MeshFormat::Stl;

    std::array<char, 80> header{};
    input.read(header.data(), static_cast<std::streamsize>(header.size()));

    std::uint32_t triangle_count = 0;
    input.read(reinterpret_cast<char*>(&triangle_count), sizeof(triangle_count));

    document.positions.reserve(static_cast<std::size_t>(triangle_count) * 3ULL);
    document.triangles.reserve(triangle_count);
    document.normals.reserve(triangle_count);

    for (std::uint32_t triangle_index = 0; triangle_index < triangle_count; ++triangle_index) {
        Vec3 normal;
        Vec3 vertices[3];
        std::uint16_t attribute_byte_count = 0;

        input.read(reinterpret_cast<char*>(&normal), sizeof(normal));
        input.read(reinterpret_cast<char*>(vertices), sizeof(vertices));
        input.read(reinterpret_cast<char*>(&attribute_byte_count), sizeof(attribute_byte_count));

        if (!input) {
            return failure("Binary STL ended unexpectedly.");
        }

        const std::uint32_t base_index = static_cast<std::uint32_t>(document.positions.size());
        document.normals.push_back(normal);
        document.positions.push_back(vertices[0]);
        document.positions.push_back(vertices[1]);
        document.positions.push_back(vertices[2]);
        document.triangles.push_back(Triangle{.a = base_index, .b = base_index + 1U, .c = base_index + 2U});

        if (((triangle_index + 1U) % 2048U) == 0U || (triangle_index + 1U) == triangle_count) {
            reportProgress(
                progress_callback,
                triangle_count == 0U ? 1.0F : static_cast<float>(triangle_index + 1U) / static_cast<float>(triangle_count),
                "Reading binary STL"
            );
        }
    }

    if (document.positions.empty()) {
        return failure("STL file does not contain any triangles.");
    }

    document.bounds = computeBounds(document.positions);
    reportProgress(progress_callback, 1.0F, "Reading binary STL");
    return MeshImportResult{.document = std::move(document), .error_message = {}};
}

MeshImportResult importAsciiStl(const std::filesystem::path& path, const MeshImportProgressCallback& progress_callback) {
    std::ifstream input(path);
    if (!input) {
        return failure("Failed to open STL file.");
    }
    const std::uintmax_t total_bytes = std::filesystem::file_size(path);
    reportProgress(progress_callback, 0.0F, "Reading ASCII STL");

    MeshDocument document;
    document.source_path = path;
    document.format = MeshFormat::Stl;

    std::string line;
    std::vector<Vec3> facet_vertices;
    Vec3 current_normal{};
    bool have_normal = false;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;
        line = trim(std::move(line));
        if (line.empty()) {
            continue;
        }

        std::istringstream stream(line);
        std::string keyword;
        stream >> keyword;
        keyword = toLower(std::move(keyword));

        if (keyword == "facet") {
            std::string normal_keyword;
            stream >> normal_keyword;
            normal_keyword = toLower(std::move(normal_keyword));
            if (normal_keyword == "normal" && (stream >> current_normal.x >> current_normal.y >> current_normal.z)) {
                have_normal = true;
            } else {
                return failure("Malformed STL facet normal at line " + std::to_string(line_number) + ".");
            }
            facet_vertices.clear();
            continue;
        }

        if (keyword == "vertex") {
            Vec3 vertex;
            if (!(stream >> vertex.x >> vertex.y >> vertex.z)) {
                return failure("Malformed STL vertex at line " + std::to_string(line_number) + ".");
            }
            facet_vertices.push_back(vertex);
            continue;
        }

        if (keyword == "endfacet") {
            if (facet_vertices.size() != 3) {
                return failure("ASCII STL facet at line " + std::to_string(line_number) + " does not have exactly 3 vertices.");
            }

            const std::uint32_t base_index = static_cast<std::uint32_t>(document.positions.size());
            document.positions.push_back(facet_vertices[0]);
            document.positions.push_back(facet_vertices[1]);
            document.positions.push_back(facet_vertices[2]);
            document.triangles.push_back(Triangle{.a = base_index, .b = base_index + 1U, .c = base_index + 2U});

            if (have_normal) {
                document.normals.push_back(current_normal);
            }

            facet_vertices.clear();
        }

        if ((line_number % 2048U) == 0U) {
            const std::streampos current_offset = input.tellg();
            if (current_offset > 0 && total_bytes > 0U) {
                reportProgress(
                    progress_callback,
                    static_cast<float>(current_offset) / static_cast<float>(total_bytes),
                    "Reading ASCII STL"
                );
            }
        }
    }

    if (document.positions.empty()) {
        return failure("ASCII STL file does not contain any triangles.");
    }

    document.bounds = computeBounds(document.positions);
    reportProgress(progress_callback, 1.0F, "Reading ASCII STL");
    return MeshImportResult{.document = std::move(document), .error_message = {}};
}

MeshImportResult importStl(const std::filesystem::path& path, const MeshImportProgressCallback& progress_callback) {
    std::ifstream probe(path, std::ios::binary);
    if (!probe) {
        return failure("Failed to open STL file.");
    }

    if (isLikelyBinaryStl(probe)) {
        return importBinaryStl(path, progress_callback);
    }

    return importAsciiStl(path, progress_callback);
}

}  // namespace

MeshImportResult importMeshFromFile(const std::filesystem::path& path, const MeshImportProgressCallback& progress_callback) {
    const std::string extension = toLower(path.extension().string());
    if (extension == ".obj") {
        return importObj(path, progress_callback);
    }
    if (extension == ".stl") {
        return importStl(path, progress_callback);
    }

    return failure("Unsupported mesh format. Supported formats: OBJ and STL.");
}

}  // namespace meshtools::io
