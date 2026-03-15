#include "meshtools/io/ProjectArchive.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "meshtools/io/MeshImporter.h"

namespace meshtools::io {
namespace {

constexpr const char* kConfigFileName = "config.ini";
constexpr const char* kProjectFileName = "file.json";
constexpr const char* kProjectLogFileName = "project.log";
constexpr const char* kResourcesDirectoryName = "resources";
constexpr const char* kEntitySetsDirectoryName = "entitysets";
constexpr const char* kDefaultMeshResourcePath = "resources/mesh.obj";

std::string trim(std::string value) {
    const auto is_space = [](unsigned char character) {
        return std::isspace(character) != 0;
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&is_space](char character) {
                    return !is_space(static_cast<unsigned char>(character));
                }));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), [&is_space](char character) {
            return !is_space(static_cast<unsigned char>(character));
        }).base(),
        value.end()
    );
    return value;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string shellEscape(const std::filesystem::path& path) {
    std::string escaped = "'";
    for (const char character : path.string()) {
        if (character == '\'') {
            escaped += "'\\''";
        } else {
            escaped += character;
        }
    }
    escaped += '\'';
    return escaped;
}

bool runShellCommand(const std::string& command) {
    return std::system(command.c_str()) == 0;
}

class ScopedTempDirectory {
  public:
    ScopedTempDirectory() {
        const std::filesystem::path base = std::filesystem::temp_directory_path();

        for (int attempt = 0; attempt < 32; ++attempt) {
            const std::filesystem::path candidate =
                base / std::filesystem::path("meshtools-project-" + std::to_string(std::rand()) + "-" + std::to_string(attempt));

            std::error_code error;
            if (std::filesystem::create_directories(candidate, error)) {
                path_ = candidate;
                return;
            }
        }

        throw std::runtime_error("Failed to create a temporary project directory.");
    }

    ~ScopedTempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open " + path.filename().string() + '.');
    }

    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

void writeTextFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Failed to write " + path.filename().string() + '.');
    }

    output << contents;
    if (!output) {
        throw std::runtime_error("Failed to write " + path.filename().string() + '.');
    }
}

class Fnv1a64 {
  public:
    void update(std::string_view value) {
        for (const char byte : value) {
            updateByte(static_cast<unsigned char>(byte));
        }
    }

    void updateBytes(std::span<const char> value) {
        for (const char byte : value) {
            updateByte(static_cast<unsigned char>(byte));
        }
    }

    [[nodiscard]] std::string hexDigest() const {
        std::ostringstream stream;
        stream << std::hex << std::nouppercase << state_;
        return stream.str();
    }

  private:
    void updateByte(unsigned char byte) {
        state_ ^= static_cast<std::uint64_t>(byte);
        state_ *= 1099511628211ULL;
    }

    std::uint64_t state_ = 14695981039346656037ULL;
};

void updateChecksumWithFile(
    Fnv1a64& checksum,
    const std::filesystem::path& base_path,
    const std::filesystem::path& absolute_path
) {
    const std::filesystem::path relative_path = std::filesystem::relative(absolute_path, base_path);
    checksum.update(relative_path.generic_string());
    checksum.update("\n");

    std::ifstream input(absolute_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to read " + relative_path.generic_string() + '.');
    }

    std::array<char, 4096> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize bytes_read = input.gcount();
        if (bytes_read > 0) {
            checksum.updateBytes(std::span<const char>(buffer.data(), static_cast<std::size_t>(bytes_read)));
        }
    }
}

std::string computeProjectChecksum(const std::filesystem::path& project_root) {
    Fnv1a64 checksum;

    std::vector<std::filesystem::path> files_to_hash;
    files_to_hash.push_back(project_root / kProjectFileName);

    const std::filesystem::path resources_path = project_root / kResourcesDirectoryName;
    if (std::filesystem::exists(resources_path)) {
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(resources_path)) {
            if (entry.is_regular_file()) {
                files_to_hash.push_back(entry.path());
            }
        }
    }

    const std::filesystem::path entity_sets_path = project_root / kEntitySetsDirectoryName;
    if (std::filesystem::exists(entity_sets_path)) {
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(entity_sets_path)) {
            if (entry.is_regular_file()) {
                files_to_hash.push_back(entry.path());
            }
        }
    }

    std::sort(files_to_hash.begin(), files_to_hash.end(), [&project_root](const auto& left, const auto& right) {
        return std::filesystem::relative(left, project_root).generic_string() <
               std::filesystem::relative(right, project_root).generic_string();
    });

    for (const std::filesystem::path& file_path : files_to_hash) {
        updateChecksumWithFile(checksum, project_root, file_path);
    }

    return checksum.hexDigest();
}

std::string serializeProjectFileJson(const mesh::MeshDocument& document, const std::string& mesh_resource_path) {
    std::ostringstream stream;
    stream << "{\n"
           << "  \"mesh\": {\n"
           << "    \"path\": \"" << mesh_resource_path << "\",\n"
           << "    \"format\": \"obj\"\n"
           << "  },\n"
           << "  \"explicit_edges\": [";
    if (!document.explicit_edges.empty()) {
        stream << '\n';
        for (std::size_t index = 0; index < document.explicit_edges.size(); ++index) {
            const mesh::EdgeSegment& edge = document.explicit_edges[index];
            stream << "    { \"a\": " << edge.a << ", \"b\": " << edge.b << " }";
            if (index + 1 < document.explicit_edges.size()) {
                stream << ',';
            }
            stream << '\n';
        }
        stream << "  ";
    }
    stream << "]\n"
           << "}\n";
    return stream.str();
}

std::optional<std::string> extractJsonStringValue(const std::string& json, const std::string& key) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch match;
    if (!std::regex_search(json, match, pattern) || match.size() < 2) {
        return std::nullopt;
    }

    return match[1].str();
}

std::vector<mesh::EdgeSegment> parseExplicitEdgesFromProjectJson(const std::string& json) {
    std::vector<mesh::EdgeSegment> edges;
    const std::regex object_pattern("\\{\\s*\"a\"\\s*:\\s*(\\d+)\\s*,\\s*\"b\"\\s*:\\s*(\\d+)\\s*\\}");
    for (std::sregex_iterator iterator(json.begin(), json.end(), object_pattern), end; iterator != end; ++iterator) {
        const unsigned long a = std::stoul((*iterator)[1].str());
        const unsigned long b = std::stoul((*iterator)[2].str());
        if (a > std::numeric_limits<std::uint32_t>::max() || b > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("Explicit edge index exceeds supported range.");
        }
        if (a == b) {
            continue;
        }
        edges.push_back(mesh::EdgeSegment{
            .a = static_cast<std::uint32_t>(a),
            .b = static_cast<std::uint32_t>(b),
        });
    }
    return edges;
}

std::vector<std::string> splitLines(const std::string& contents) {
    std::vector<std::string> lines;
    std::stringstream stream(contents);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

std::string joinLines(std::span<const std::string> lines) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        stream << lines[index];
        if (index + 1 < lines.size()) {
            stream << '\n';
        }
    }
    return stream.str();
}

std::string joinIndices(std::span<const std::uint32_t> indices) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < indices.size(); ++index) {
        if (index > 0) {
            stream << ',';
        }
        stream << indices[index];
    }
    return stream.str();
}

std::vector<std::uint32_t> parseIndices(const std::string& value) {
    std::vector<std::uint32_t> indices;
    std::stringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token = trim(std::move(token));
        if (token.empty()) {
            continue;
        }

        std::size_t consumed = 0;
        const unsigned long parsed = std::stoul(token, &consumed, 10);
        if (consumed != token.size() || parsed > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("Invalid entity index value: " + token + '.');
        }

        indices.push_back(static_cast<std::uint32_t>(parsed));
    }

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

std::optional<std::filesystem::path> locateProjectRoot(const std::filesystem::path& extract_root) {
    const auto is_project_root = [](const std::filesystem::path& candidate) {
        return std::filesystem::exists(candidate / kConfigFileName) &&
               std::filesystem::exists(candidate / kProjectFileName) &&
               std::filesystem::exists(candidate / kProjectLogFileName) &&
               std::filesystem::is_directory(candidate / kResourcesDirectoryName);
    };

    if (is_project_root(extract_root)) {
        return extract_root;
    }

    std::vector<std::filesystem::path> candidates;
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(extract_root)) {
        if (!entry.is_regular_file() || entry.path().filename() != kConfigFileName) {
            continue;
        }

        const std::filesystem::path candidate = entry.path().parent_path();
        if (is_project_root(candidate)) {
            candidates.push_back(candidate);
        }
    }

    if (candidates.size() == 1) {
        return candidates.front();
    }

    return std::nullopt;
}

std::optional<std::string> readIniValue(const std::string& contents, const std::string& key) {
    std::stringstream stream(contents);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        line = trim(std::move(line));
        if (line.empty() || line.front() == '#' || line.front() == ';' || line.front() == '[') {
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string current_key = trim(line.substr(0, separator));
        if (current_key != key) {
            continue;
        }

        return trim(line.substr(separator + 1));
    }

    return std::nullopt;
}

std::string serializeConfigIni(std::string checksum, mesh::UpAxis up_axis) {
    return "[project]\n"
           "format_version=" + std::string(kProjectFormatVersion) + "\n"
           "checksum=" + std::move(checksum) + "\n"
           "up_axis=" + std::string(mesh::upAxisName(up_axis)) + "\n";
}

std::string sanitizeEntitySetFilenameStem(std::string name) {
    name = trim(std::move(name));
    if (name.empty()) {
        return "Entity Set";
    }

    for (char& character : name) {
        const bool allowed =
            std::isalnum(static_cast<unsigned char>(character)) != 0 ||
            character == ' ' ||
            character == '-' ||
            character == '_' ||
            character == '(' ||
            character == ')';
        if (!allowed) {
            character = '_';
        }
    }

    name = trim(std::move(name));
    return name.empty() ? std::string("Entity Set") : name;
}

std::filesystem::path uniqueEntitySetFilePath(
    const std::filesystem::path& directory,
    std::string_view entity_set_name,
    std::vector<std::filesystem::path>* used_paths
) {
    const std::string stem = sanitizeEntitySetFilenameStem(std::string(entity_set_name));
    std::size_t suffix = 1;
    while (true) {
        const std::string candidate_stem =
            suffix == 1 ? stem : (stem + ' ' + std::to_string(suffix));
        const std::filesystem::path candidate = directory / (candidate_stem + ".entityset");
        const bool already_used = std::find(used_paths->begin(), used_paths->end(), candidate) != used_paths->end();
        if (!already_used) {
            used_paths->push_back(candidate);
            return candidate;
        }
        ++suffix;
    }
}

std::string serializeEntitySetFile(const mesh::EntitySet& entity_set) {
    return "name=" + entity_set.name + "\n"
           "faces=" + joinIndices(entity_set.members.face_indices) + "\n"
           "edges=" + joinIndices(entity_set.members.edge_indices) + "\n"
           "points=" + joinIndices(entity_set.members.point_indices) + "\n";
}

mesh::UpAxis parseUpAxis(const std::string& value) {
    const std::string normalized = toLower(value);
    if (normalized == "y") {
        return mesh::UpAxis::Y;
    }
    if (normalized == "z") {
        return mesh::UpAxis::Z;
    }

    throw std::runtime_error("Unsupported project up axis: " + value + '.');
}

mesh::EntitySet parseEntitySetFile(const std::filesystem::path& path, const std::string& contents) {
    const std::optional<std::string> name = readIniValue(contents, "name");
    if (!name.has_value()) {
        throw std::runtime_error("Entity set file is missing name: " + path.filename().string() + '.');
    }

    const std::optional<std::string> faces = readIniValue(contents, "faces");
    const std::optional<std::string> edges = readIniValue(contents, "edges");
    const std::optional<std::string> points = readIniValue(contents, "points");

    return mesh::EntitySet{
        .name = *name,
        .members = mesh::EntitySelection{
            .edge_indices = edges.has_value() ? parseIndices(*edges) : std::vector<std::uint32_t>{},
            .face_indices = faces.has_value() ? parseIndices(*faces) : std::vector<std::uint32_t>{},
            .point_indices = points.has_value() ? parseIndices(*points) : std::vector<std::uint32_t>{},
        },
    };
}

void exportMeshAsObj(const mesh::MeshDocument& document, const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Failed to create mesh resource.");
    }

    for (const mesh::Vec3& position : document.positions) {
        output << "v " << position.x << ' ' << position.y << ' ' << position.z << '\n';
    }

    for (const mesh::Triangle& triangle : document.triangles) {
        output << "f "
               << (triangle.a + 1U) << ' '
               << (triangle.b + 1U) << ' '
               << (triangle.c + 1U) << '\n';
    }

    if (!output) {
        throw std::runtime_error("Failed to write mesh resource.");
    }
}

ProjectArchiveLoadResult failure(std::string message) {
    return ProjectArchiveLoadResult{
        .document = std::nullopt,
        .log_messages = {},
        .error_message = std::move(message),
    };
}

ProjectArchiveSaveResult saveFailure(std::string message) {
    return ProjectArchiveSaveResult{.error_message = std::move(message)};
}

void reportProgress(
    const ProjectArchiveLoadProgressCallback& progress_callback,
    float progress,
    std::string_view stage
) {
    if (!progress_callback) {
        return;
    }

    progress_callback(std::clamp(progress, 0.0F, 1.0F), stage);
}

}  // namespace

ProjectArchiveLoadResult loadProjectArchive(
    const std::filesystem::path& archive_path,
    const ProjectArchiveLoadProgressCallback& progress_callback
) {
#if defined(_WIN32)
    (void)progress_callback;
    return failure("Project archives are not supported on Windows builds yet.");
#else
    try {
        reportProgress(progress_callback, 0.0F, "Unpacking project");
        ScopedTempDirectory temp_directory;
        const std::filesystem::path extract_root = temp_directory.path() / "extract";
        std::filesystem::create_directories(extract_root);

        const std::string unzip_command =
            "/usr/bin/unzip -q " + shellEscape(archive_path) + " -d " + shellEscape(extract_root);
        if (!runShellCommand(unzip_command)) {
            return failure("Failed to unpack project archive.");
        }
        reportProgress(progress_callback, 0.15F, "Validating project");

        const std::optional<std::filesystem::path> project_root = locateProjectRoot(extract_root);
        if (!project_root.has_value()) {
            return failure("Project archive is missing config.ini, file.json, project.log, or resources/.");
        }

        const std::string config_ini = readTextFile(*project_root / kConfigFileName);
        const std::optional<std::string> format_version = readIniValue(config_ini, "format_version");
        if (!format_version.has_value()) {
            return failure("Project archive is missing format_version in config.ini.");
        }
        if (*format_version != kProjectFormatVersion) {
            return failure("Unsupported project format version: " + *format_version + '.');
        }

        const std::optional<std::string> expected_checksum = readIniValue(config_ini, "checksum");
        if (!expected_checksum.has_value()) {
            return failure("Project archive is missing checksum in config.ini.");
        }

        reportProgress(progress_callback, 0.3F, "Verifying project checksum");
        const std::string actual_checksum = computeProjectChecksum(*project_root);
        if (actual_checksum != *expected_checksum) {
            return failure("Project checksum mismatch. The project may have been modified outside MeshTools.");
        }

        const std::optional<std::string> up_axis_value = readIniValue(config_ini, "up_axis");
        if (!up_axis_value.has_value()) {
            return failure("Project archive is missing up_axis in config.ini.");
        }

        const std::string project_json = readTextFile(*project_root / kProjectFileName);
        const std::optional<std::string> mesh_resource_path = extractJsonStringValue(project_json, "path");
        if (!mesh_resource_path.has_value()) {
            return failure("Project file.json is missing mesh.path.");
        }

        const std::filesystem::path resolved_mesh_path = *project_root / std::filesystem::path(*mesh_resource_path);
        if (!std::filesystem::exists(resolved_mesh_path)) {
            return failure("Project mesh resource is missing: " + *mesh_resource_path + '.');
        }

        reportProgress(progress_callback, 0.45F, "Loading mesh resource");
        MeshImportResult import_result = importMeshFromFile(
            resolved_mesh_path,
            [&progress_callback](float progress, std::string_view stage) {
                reportProgress(progress_callback, 0.45F + (progress * 0.4F), stage);
            }
        );
        if (!import_result.succeeded()) {
            return failure("Failed to load project mesh: " + import_result.error_message);
        }

        import_result.document->source_path = archive_path;
        import_result.document->display_name_override = archive_path.stem().string();
        import_result.document->up_axis = parseUpAxis(*up_axis_value);
        import_result.document->explicit_edges = parseExplicitEdgesFromProjectJson(project_json);

        const std::filesystem::path entity_sets_path = *project_root / kEntitySetsDirectoryName;
        if (std::filesystem::is_directory(entity_sets_path)) {
            std::vector<std::filesystem::path> entity_set_files;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(entity_sets_path)) {
                if (entry.is_regular_file() && toLower(entry.path().extension().string()) == ".entityset") {
                    entity_set_files.push_back(entry.path());
                }
            }

            std::sort(entity_set_files.begin(), entity_set_files.end());
            for (const std::filesystem::path& entity_set_path : entity_set_files) {
                import_result.document->entity_sets.push_back(
                    parseEntitySetFile(entity_set_path, readTextFile(entity_set_path))
                );
            }
        }

        reportProgress(progress_callback, 0.95F, "Loading project metadata");

        ProjectArchiveLoadResult result{
            .document = std::move(import_result.document),
            .log_messages = splitLines(readTextFile(*project_root / kProjectLogFileName)),
            .error_message = {},
        };
        reportProgress(progress_callback, 1.0F, "Loading project metadata");
        return result;
    } catch (const std::exception& exception) {
        return failure(exception.what());
    }
#endif
}

ProjectArchiveSaveResult saveProjectArchive(
    const std::filesystem::path& archive_path,
    const ProjectArchiveSaveInput& input
) {
#if defined(_WIN32)
    (void)archive_path;
    (void)input;
    return saveFailure("Project archives are not supported on Windows builds yet.");
#else
    try {
        ScopedTempDirectory temp_directory;
        const std::filesystem::path project_root = temp_directory.path() / "project";
        const std::filesystem::path resources_path = project_root / kResourcesDirectoryName;
        const std::filesystem::path entity_sets_path = project_root / kEntitySetsDirectoryName;
        std::filesystem::create_directories(resources_path);
        std::filesystem::create_directories(entity_sets_path);

        exportMeshAsObj(input.document, project_root / kDefaultMeshResourcePath);
        writeTextFile(project_root / kProjectFileName, serializeProjectFileJson(input.document, kDefaultMeshResourcePath));
        writeTextFile(project_root / kProjectLogFileName, joinLines(input.log_messages));
        std::vector<std::filesystem::path> entity_set_paths;
        entity_set_paths.reserve(input.document.entity_sets.size());
        for (const mesh::EntitySet& entity_set : input.document.entity_sets) {
            writeTextFile(
                uniqueEntitySetFilePath(entity_sets_path, entity_set.name, &entity_set_paths),
                serializeEntitySetFile(entity_set)
            );
        }

        const std::string checksum = computeProjectChecksum(project_root);
        writeTextFile(project_root / kConfigFileName, serializeConfigIni(checksum, input.document.up_axis));

        const std::filesystem::path temp_archive_path = temp_directory.path() / "project.mt";
        const std::string zip_command =
            "/bin/sh -c \"cd " + shellEscape(project_root) +
            " && /usr/bin/zip -q -r " + shellEscape(temp_archive_path) +
            " config.ini file.json project.log resources entitysets\"";
        if (!runShellCommand(zip_command)) {
            return saveFailure("Failed to build project archive.");
        }

        std::error_code error;
        std::filesystem::create_directories(archive_path.parent_path(), error);
        std::filesystem::remove(archive_path, error);
        error.clear();
        std::filesystem::rename(temp_archive_path, archive_path, error);
        if (error) {
            return saveFailure("Failed to move project archive to " + archive_path.string() + '.');
        }

        return ProjectArchiveSaveResult{};
    } catch (const std::exception& exception) {
        return saveFailure(exception.what());
    }
#endif
}

}  // namespace meshtools::io
