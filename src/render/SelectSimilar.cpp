#include "meshtools/render/SelectSimilar.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ViewportRendererDetail.h"

namespace meshtools::render {
namespace {

constexpr std::size_t kMaxSelectedEdges = 12;
constexpr float kAxisAngleEpsilon = 0.05F;
constexpr float kBasisEpsilon = 1.0e-5F;

struct EdgeInfo {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    float length = 0.0F;
};

struct Pattern {
    std::vector<std::uint32_t> global_vertices;
    std::vector<EdgeInfo> edges;
    std::vector<std::vector<std::uint32_t>> incident_edges;
    std::vector<std::uint32_t> degrees;
    std::unordered_map<std::uint64_t, std::uint32_t> edge_lookup;
    std::vector<float> pairwise_distances;
    std::size_t anchor_edge_index = 0;
    float max_distance = 0.0F;
    std::optional<std::uint32_t> reference_vertex;
};

struct Basis {
    mesh::Vec3 x;
    mesh::Vec3 y;
    mesh::Vec3 z;
};

struct EdgeSetHash {
    [[nodiscard]] std::size_t operator()(const std::vector<std::uint32_t>& values) const noexcept {
        std::size_t seed = 0;
        for (const std::uint32_t value : values) {
            seed ^= static_cast<std::size_t>(value) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
        }
        return seed;
    }
};

struct MatchContext {
    const std::vector<mesh::Vec3>& positions;
    const std::vector<EdgeInfo>& all_edges;
    const std::vector<std::vector<std::uint32_t>>& incident_edges;
    const std::unordered_map<std::uint64_t, std::uint32_t>& edge_lookup;
    const Pattern& pattern;
    const SelectSimilarParams& params;
    std::vector<std::uint32_t> current_selection;
    std::unordered_set<std::vector<std::uint32_t>, EdgeSetHash> accepted;
    std::vector<std::vector<std::uint32_t>> matches;
    std::size_t recursion_budget = 20000;
    std::size_t branch_counter = 0;
};

void sortAndUnique(std::vector<std::uint32_t>* indices) {
    if (indices == nullptr) {
        return;
    }
    std::sort(indices->begin(), indices->end());
    indices->erase(std::unique(indices->begin(), indices->end()), indices->end());
}

float relativeToleranceFraction(const SelectSimilarParams& params) {
    return std::max(params.tolerance, 0.0F) * 0.01F;
}

float scaledTolerance(float reference_length, float tolerance_fraction) {
    return std::max(std::max(reference_length, 1.0e-5F) * tolerance_fraction, 1.0e-6F);
}

float edgeLength(const mesh::Vec3& a, const mesh::Vec3& b) {
    return detail::length(detail::subtract(b, a));
}

std::uint32_t otherVertex(const EdgeInfo& edge, std::uint32_t vertex) {
    return edge.a == vertex ? edge.b : edge.a;
}

bool contains(const std::vector<std::uint32_t>& values, std::uint32_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

mesh::Vec3 chooseReferenceAxis(const mesh::Vec3& axis) {
    const float ax = std::abs(axis.x);
    const float ay = std::abs(axis.y);
    const float az = std::abs(axis.z);
    if (ax <= ay && ax <= az) {
        return mesh::Vec3{.x = 1.0F};
    }
    if (ay <= az) {
        return mesh::Vec3{.y = 1.0F};
    }
    return mesh::Vec3{.z = 1.0F};
}

std::vector<EdgeInfo> buildEdgeInfo(
    const std::vector<mesh::Vec3>& positions,
    std::span<const SimilarityEdge> edges
) {
    std::vector<EdgeInfo> info;
    info.reserve(edges.size());
    for (const SimilarityEdge& edge : edges) {
        if (edge.a >= positions.size() || edge.b >= positions.size()) {
            info.push_back(EdgeInfo{.a = edge.a, .b = edge.b});
            continue;
        }
        info.push_back(EdgeInfo{
            .a = edge.a,
            .b = edge.b,
            .length = edgeLength(positions[edge.a], positions[edge.b]),
        });
    }
    return info;
}

std::vector<std::vector<std::uint32_t>> buildIncidentEdges(std::size_t vertex_count, const std::vector<EdgeInfo>& edges) {
    std::vector<std::vector<std::uint32_t>> incident(vertex_count);
    for (std::size_t edge_index = 0; edge_index < edges.size(); ++edge_index) {
        const EdgeInfo& edge = edges[edge_index];
        if (edge.a < incident.size()) {
            incident[edge.a].push_back(static_cast<std::uint32_t>(edge_index));
        }
        if (edge.b < incident.size()) {
            incident[edge.b].push_back(static_cast<std::uint32_t>(edge_index));
        }
    }
    return incident;
}

std::unordered_map<std::uint64_t, std::uint32_t> buildEdgeLookup(const std::vector<EdgeInfo>& edges) {
    std::unordered_map<std::uint64_t, std::uint32_t> lookup;
    lookup.reserve(edges.size());
    for (std::size_t edge_index = 0; edge_index < edges.size(); ++edge_index) {
        lookup.emplace(detail::edgeKey(edges[edge_index].a, edges[edge_index].b), static_cast<std::uint32_t>(edge_index));
    }
    return lookup;
}

std::optional<Pattern> buildPattern(
    const std::vector<mesh::Vec3>& positions,
    const std::vector<EdgeInfo>& all_edges,
    const std::vector<std::uint32_t>& selected_edge_indices
) {
    if (selected_edge_indices.empty()) {
        return std::nullopt;
    }

    Pattern pattern;
    std::unordered_map<std::uint32_t, std::uint32_t> local_by_global;
    local_by_global.reserve(selected_edge_indices.size() * 2ULL);
    std::vector<std::vector<std::uint32_t>> selected_incident;

    for (const std::uint32_t edge_index : selected_edge_indices) {
        if (edge_index >= all_edges.size()) {
            return std::nullopt;
        }
        const EdgeInfo& edge = all_edges[edge_index];
        const auto map_vertex = [&](std::uint32_t vertex) {
            const auto [iterator, inserted] =
                local_by_global.emplace(vertex, static_cast<std::uint32_t>(pattern.global_vertices.size()));
            if (inserted) {
                pattern.global_vertices.push_back(vertex);
                selected_incident.emplace_back();
            }
            return iterator->second;
        };
        const std::uint32_t local_a = map_vertex(edge.a);
        const std::uint32_t local_b = map_vertex(edge.b);
        const std::uint32_t local_edge_index = static_cast<std::uint32_t>(pattern.edges.size());
        pattern.edges.push_back(EdgeInfo{.a = local_a, .b = local_b, .length = edge.length});
        selected_incident[local_a].push_back(local_edge_index);
        selected_incident[local_b].push_back(local_edge_index);
        pattern.edge_lookup.emplace(detail::edgeKey(local_a, local_b), local_edge_index);
    }

    std::vector<unsigned char> visited(pattern.edges.size(), 0);
    std::vector<std::uint32_t> frontier{0};
    visited[0] = 1;
    while (!frontier.empty()) {
        const std::uint32_t edge_index = frontier.back();
        frontier.pop_back();
        const EdgeInfo& edge = pattern.edges[edge_index];
        for (const std::uint32_t vertex : {edge.a, edge.b}) {
            for (const std::uint32_t neighbor_edge : selected_incident[vertex]) {
                if (visited[neighbor_edge] == 0) {
                    visited[neighbor_edge] = 1;
                    frontier.push_back(neighbor_edge);
                }
            }
        }
    }
    if (std::find(visited.begin(), visited.end(), 0) != visited.end()) {
        return std::nullopt;
    }

    pattern.incident_edges = std::move(selected_incident);
    pattern.degrees.reserve(pattern.incident_edges.size());
    for (const auto& incident : pattern.incident_edges) {
        pattern.degrees.push_back(static_cast<std::uint32_t>(incident.size()));
    }
    const std::size_t vertex_count = pattern.global_vertices.size();
    pattern.pairwise_distances.assign(vertex_count * vertex_count, 0.0F);
    for (std::size_t left = 0; left < vertex_count; ++left) {
        for (std::size_t right = left + 1; right < vertex_count; ++right) {
            const float distance = edgeLength(
                positions[pattern.global_vertices[left]],
                positions[pattern.global_vertices[right]]
            );
            pattern.pairwise_distances[(left * vertex_count) + right] = distance;
            pattern.pairwise_distances[(right * vertex_count) + left] = distance;
            pattern.max_distance = std::max(pattern.max_distance, distance);
        }
    }

    pattern.anchor_edge_index = 0;
    for (std::size_t edge_index = 1; edge_index < pattern.edges.size(); ++edge_index) {
        if (pattern.edges[edge_index].length > pattern.edges[pattern.anchor_edge_index].length) {
            pattern.anchor_edge_index = edge_index;
        }
    }

    const EdgeInfo& anchor = pattern.edges[pattern.anchor_edge_index];
    const mesh::Vec3 origin = positions[pattern.global_vertices[anchor.a]];
    const mesh::Vec3 axis = detail::normalize(detail::subtract(positions[pattern.global_vertices[anchor.b]], origin));
    float best_distance = 0.0F;
    for (std::uint32_t local_vertex = 0; local_vertex < pattern.global_vertices.size(); ++local_vertex) {
        if (local_vertex == anchor.a || local_vertex == anchor.b) {
            continue;
        }
        const mesh::Vec3 delta = detail::subtract(positions[pattern.global_vertices[local_vertex]], origin);
        const mesh::Vec3 perpendicular = detail::subtract(delta, detail::scale(axis, detail::dot(delta, axis)));
        const float distance = detail::length(perpendicular);
        if (distance > best_distance) {
            best_distance = distance;
            pattern.reference_vertex = local_vertex;
        }
    }

    return pattern;
}

Basis makeBasis(
    const std::vector<mesh::Vec3>& positions,
    std::span<const std::uint32_t> vertices,
    const EdgeInfo& anchor,
    std::optional<std::uint32_t> reference_vertex
) {
    const mesh::Vec3 origin = positions[vertices[anchor.a]];
    Basis basis{.x = detail::normalize(detail::subtract(positions[vertices[anchor.b]], origin))};
    mesh::Vec3 reference = reference_vertex.has_value()
        ? detail::subtract(positions[vertices[*reference_vertex]], origin)
        : chooseReferenceAxis(basis.x);
    reference = detail::subtract(reference, detail::scale(basis.x, detail::dot(reference, basis.x)));
    if (detail::length(reference) <= kBasisEpsilon) {
        reference = detail::subtract(chooseReferenceAxis(basis.x), detail::scale(basis.x, detail::dot(chooseReferenceAxis(basis.x), basis.x)));
    }
    basis.y = detail::normalize(reference);
    basis.z = detail::normalize(detail::cross(basis.x, basis.y));
    basis.y = detail::normalize(detail::cross(basis.z, basis.x));
    return basis;
}

mesh::Vec3 rotateBetweenBases(const Basis& source, const Basis& target, const mesh::Vec3& value) {
    const mesh::Vec3 source_coords{
        .x = detail::dot(value, source.x),
        .y = detail::dot(value, source.y),
        .z = detail::dot(value, source.z),
    };
    return detail::add(
        detail::add(detail::scale(target.x, source_coords.x), detail::scale(target.y, source_coords.y)),
        detail::scale(target.z, source_coords.z)
    );
}

bool rotationAllowed(const Basis& source, const Basis& target, const SelectSimilarParams& params) {
    const auto element = [&](int row, int column) {
        const std::array<mesh::Vec3, 3> s = {source.x, source.y, source.z};
        const std::array<mesh::Vec3, 3> t = {target.x, target.y, target.z};
        const std::size_t column_index = static_cast<std::size_t>(column);
        const std::array<float, 3> values = {
            detail::dot(t[0], s[column_index]),
            detail::dot(t[1], s[column_index]),
            detail::dot(t[2], s[column_index]),
        };
        return values[static_cast<std::size_t>(row)];
    };

    const float m00 = element(0, 0);
    const float m01 = element(0, 1);
    const float m02 = element(0, 2);
    const float m12 = element(1, 2);
    const float m22 = element(2, 2);
    const float m10 = element(1, 0);
    const float m11 = element(1, 1);

    const float y = std::asin(std::clamp(m02, -1.0F, 1.0F));
    const bool gimbal_lock = std::abs(std::cos(y)) <= 1.0e-4F;
    const float x = gimbal_lock ? std::atan2(m10, m11) : std::atan2(-m12, m22);
    const float z = gimbal_lock ? 0.0F : std::atan2(-m01, m00);
    return (params.allow_rotation_x || std::abs(x) <= kAxisAngleEpsilon) &&
           (params.allow_rotation_y || std::abs(y) <= kAxisAngleEpsilon) &&
           (params.allow_rotation_z || std::abs(z) <= kAxisAngleEpsilon);
}

bool compatibleLength(float expected, float actual, float tolerance_fraction) {
    return std::abs(expected - actual) <= scaledTolerance(std::max(expected, actual), tolerance_fraction);
}

float pairwiseDistance(const Pattern& pattern, std::uint32_t left, std::uint32_t right) {
    const std::size_t vertex_count = pattern.global_vertices.size();
    return pattern.pairwise_distances[(static_cast<std::size_t>(left) * vertex_count) + right];
}

bool rotationUnrestricted(const SelectSimilarParams& params) {
    return params.allow_rotation_x && params.allow_rotation_y && params.allow_rotation_z;
}

std::string formatIndices(const std::vector<std::uint32_t>& values) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << ", ";
        }
        stream << values[index];
    }
    stream << ']';
    return stream.str();
}

void debugLog(const std::string& message) {
    std::cout << "[SS] " << message << std::endl;
}

void debugLogEdgeInfo(
    const std::vector<mesh::Vec3>& positions,
    const std::vector<EdgeInfo>& edges,
    const std::vector<std::uint32_t>& edge_indices,
    const char* label
) {
    debugLog(std::string(label) + " count=" + std::to_string(edge_indices.size()) + " indices=" + formatIndices(edge_indices));
    for (const std::uint32_t edge_index : edge_indices) {
        if (edge_index >= edges.size()) {
            debugLog("  edge " + std::to_string(edge_index) + " is out of range");
            continue;
        }
        const EdgeInfo& edge = edges[edge_index];
        std::ostringstream stream;
        stream
            << "  edge " << edge_index
            << " (" << edge.a << " -> " << edge.b << ")"
            << " length=" << edge.length;
        if (edge.a < positions.size() && edge.b < positions.size()) {
            stream
                << " a=(" << positions[edge.a].x << ", " << positions[edge.a].y << ", " << positions[edge.a].z << ')'
                << " b=(" << positions[edge.b].x << ", " << positions[edge.b].y << ", " << positions[edge.b].z << ')';
        }
        debugLog(stream.str());
    }
}

void debugLogPattern(
    const std::vector<mesh::Vec3>& positions,
    const Pattern& pattern
) {
    debugLog(
        "pattern vertices=" + std::to_string(pattern.global_vertices.size()) +
        " edges=" + std::to_string(pattern.edges.size()) +
        " anchor_edge=" + std::to_string(pattern.anchor_edge_index) +
        " reference_vertex=" +
        (pattern.reference_vertex.has_value() ? std::to_string(*pattern.reference_vertex) : std::string("none"))
    );
    for (std::size_t local_vertex = 0; local_vertex < pattern.global_vertices.size(); ++local_vertex) {
        const std::uint32_t global_vertex = pattern.global_vertices[local_vertex];
        std::ostringstream stream;
        stream
            << "  vertex local=" << local_vertex
            << " global=" << global_vertex
            << " degree=" << pattern.degrees[local_vertex];
        if (global_vertex < positions.size()) {
            stream
                << " pos=(" << positions[global_vertex].x << ", " << positions[global_vertex].y << ", " << positions[global_vertex].z << ')';
        }
        debugLog(stream.str());
    }
    for (std::size_t left = 0; left < pattern.global_vertices.size(); ++left) {
        for (std::size_t right = left + 1; right < pattern.global_vertices.size(); ++right) {
            debugLog(
                "  pair distance local(" + std::to_string(left) + ", " + std::to_string(right) +
                ") = " + std::to_string(pairwiseDistance(pattern, static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(right)))
            );
        }
    }
}

bool validateMatch(const MatchContext& context, const std::vector<std::uint32_t>& vertex_map, std::vector<std::uint32_t>* matched_edges) {
    const EdgeInfo& anchor = context.pattern.edges[context.pattern.anchor_edge_index];
    const auto edge_iterator = context.edge_lookup.find(detail::edgeKey(vertex_map[anchor.a], vertex_map[anchor.b]));
    if (edge_iterator == context.edge_lookup.end()) {
        debugLog("validate rejected: anchor edge missing in candidate mapping");
        return false;
    }
    const float tolerance_fraction = relativeToleranceFraction(context.params);

    for (std::size_t left = 0; left < vertex_map.size(); ++left) {
        for (std::size_t right = left + 1; right < vertex_map.size(); ++right) {
            const bool selected_has_edge = context.pattern.edge_lookup.contains(
                detail::edgeKey(static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(right))
            );
            const bool candidate_has_edge = context.edge_lookup.contains(detail::edgeKey(vertex_map[left], vertex_map[right]));
            if (selected_has_edge != candidate_has_edge) {
                debugLog(
                    "validate rejected: adjacency mismatch local(" + std::to_string(left) + ", " +
                    std::to_string(right) + ") selected_has_edge=" +
                    std::to_string(selected_has_edge ? 1 : 0) + " candidate_has_edge=" +
                    std::to_string(candidate_has_edge ? 1 : 0)
                );
                return false;
            }
        }
    }

    const float anchor_scale =
        context.params.allow_scaling
            ? context.all_edges[edge_iterator->second].length / std::max(anchor.length, kBasisEpsilon)
            : 1.0F;
    for (std::size_t left = 0; left < vertex_map.size(); ++left) {
        for (std::size_t right = left + 1; right < vertex_map.size(); ++right) {
            const float expected_distance =
                pairwiseDistance(context.pattern, static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(right));
            const float actual_distance =
                edgeLength(context.positions[vertex_map[left]], context.positions[vertex_map[right]]);
            if (!context.params.allow_scaling || context.params.require_uniform_scaling) {
                if (!compatibleLength(expected_distance * anchor_scale, actual_distance, tolerance_fraction)) {
                    debugLog(
                        "validate rejected: pair distance mismatch local(" + std::to_string(left) + ", " +
                        std::to_string(right) + ") expected=" + std::to_string(expected_distance * anchor_scale) +
                        " actual=" + std::to_string(actual_distance)
                    );
                    return false;
                }
            }
        }
    }

    if (rotationUnrestricted(context.params) && (!context.params.allow_scaling || context.params.require_uniform_scaling)) {
        matched_edges->clear();
        matched_edges->reserve(context.pattern.edges.size());
        for (const EdgeInfo& edge : context.pattern.edges) {
            matched_edges->push_back(context.edge_lookup.at(detail::edgeKey(vertex_map[edge.a], vertex_map[edge.b])));
        }
        sortAndUnique(matched_edges);
        return true;
    }

    if (!(context.params.allow_rotation_x || context.params.allow_rotation_y || context.params.allow_rotation_z)) {
        const mesh::Vec3 source_origin = context.positions[context.pattern.global_vertices[anchor.a]];
        const mesh::Vec3 candidate_origin = context.positions[vertex_map[anchor.a]];
        const float uniform_scale = !context.params.allow_scaling
            ? 1.0F
            : context.params.require_uniform_scaling
                ? context.all_edges[edge_iterator->second].length / std::max(anchor.length, kBasisEpsilon)
                : 1.0F;
        const float absolute_tolerance =
            scaledTolerance(context.pattern.max_distance * std::max(std::abs(uniform_scale), 1.0F), tolerance_fraction);
        for (std::size_t index = 0; index < vertex_map.size(); ++index) {
            const mesh::Vec3 transformed = detail::add(
                candidate_origin,
                detail::scale(detail::subtract(context.positions[context.pattern.global_vertices[index]], source_origin), uniform_scale)
            );
            if (detail::length(detail::subtract(transformed, context.positions[vertex_map[index]])) > absolute_tolerance) {
                debugLog("validate rejected: no-rotation transform mismatch at vertex " + std::to_string(index));
                return false;
            }
        }
    } else {
        const Basis source_basis = makeBasis(context.positions, context.pattern.global_vertices, anchor, context.pattern.reference_vertex);
        const Basis candidate_basis = makeBasis(context.positions, vertex_map, anchor, context.pattern.reference_vertex);
        if (!rotationAllowed(source_basis, candidate_basis, context.params)) {
            debugLog("validate rejected: recovered rotation violates rotation axis flags");
            return false;
        }

        const mesh::Vec3 source_origin = context.positions[context.pattern.global_vertices[anchor.a]];
        const mesh::Vec3 candidate_origin = context.positions[vertex_map[anchor.a]];
        mesh::Vec3 scale{.x = 1.0F, .y = 1.0F, .z = 1.0F};
        float scale_reference = 1.0F;
        if (context.params.allow_scaling) {
            if (context.params.require_uniform_scaling) {
                float numerator = 0.0F;
                float denominator = 0.0F;
                for (std::size_t index = 0; index < vertex_map.size(); ++index) {
                    const mesh::Vec3 rotated =
                        rotateBetweenBases(source_basis, candidate_basis, detail::subtract(context.positions[context.pattern.global_vertices[index]], source_origin));
                    const mesh::Vec3 target = detail::subtract(context.positions[vertex_map[index]], candidate_origin);
                    numerator += detail::dot(rotated, target);
                    denominator += detail::dot(rotated, rotated);
                }
                const float uniform_scale = denominator <= kBasisEpsilon ? 1.0F : numerator / denominator;
                scale = mesh::Vec3{.x = uniform_scale, .y = uniform_scale, .z = uniform_scale};
                scale_reference = std::abs(uniform_scale);
            } else {
                mesh::Vec3 numerator{};
                mesh::Vec3 denominator{};
                for (std::size_t index = 0; index < vertex_map.size(); ++index) {
                    const mesh::Vec3 rotated =
                        rotateBetweenBases(source_basis, candidate_basis, detail::subtract(context.positions[context.pattern.global_vertices[index]], source_origin));
                    const mesh::Vec3 target = detail::subtract(context.positions[vertex_map[index]], candidate_origin);
                    numerator.x += rotated.x * target.x;
                    numerator.y += rotated.y * target.y;
                    numerator.z += rotated.z * target.z;
                    denominator.x += rotated.x * rotated.x;
                    denominator.y += rotated.y * rotated.y;
                    denominator.z += rotated.z * rotated.z;
                }
                scale.x = denominator.x <= kBasisEpsilon ? 1.0F : numerator.x / denominator.x;
                scale.y = denominator.y <= kBasisEpsilon ? 1.0F : numerator.y / denominator.y;
                scale.z = denominator.z <= kBasisEpsilon ? 1.0F : numerator.z / denominator.z;
                scale_reference = std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z), 1.0F});
            }
        }
        const float absolute_tolerance =
            scaledTolerance(context.pattern.max_distance * scale_reference, tolerance_fraction);

        for (std::size_t index = 0; index < vertex_map.size(); ++index) {
            mesh::Vec3 rotated =
                rotateBetweenBases(source_basis, candidate_basis, detail::subtract(context.positions[context.pattern.global_vertices[index]], source_origin));
            rotated = mesh::Vec3{.x = rotated.x * scale.x, .y = rotated.y * scale.y, .z = rotated.z * scale.z};
            const mesh::Vec3 transformed = detail::add(candidate_origin, rotated);
            if (detail::length(detail::subtract(transformed, context.positions[vertex_map[index]])) > absolute_tolerance) {
                debugLog("validate rejected: transformed vertex mismatch at vertex " + std::to_string(index));
                return false;
            }
        }
    }

    matched_edges->clear();
    matched_edges->reserve(context.pattern.edges.size());
    for (const EdgeInfo& edge : context.pattern.edges) {
        matched_edges->push_back(context.edge_lookup.at(detail::edgeKey(vertex_map[edge.a], vertex_map[edge.b])));
    }
    sortAndUnique(matched_edges);
    debugLog("validate accepted match=" + formatIndices(*matched_edges));
    return true;
}

void searchMatches(
    MatchContext* context,
    std::vector<std::uint32_t>* vertex_map,
    std::vector<unsigned char>* used_vertices
) {
    if (context == nullptr || vertex_map == nullptr || used_vertices == nullptr || context->recursion_budget-- == 0) {
        if (context != nullptr && context->recursion_budget == 0) {
            debugLog("search stopped: recursion budget exhausted");
        }
        return;
    }

    std::optional<std::uint32_t> best_edge;
    std::vector<std::uint32_t> options;
    for (std::uint32_t edge_index = 0; edge_index < context->pattern.edges.size(); ++edge_index) {
        const EdgeInfo& edge = context->pattern.edges[edge_index];
        const std::uint32_t mapped_a = (*vertex_map)[edge.a];
        const std::uint32_t mapped_b = (*vertex_map)[edge.b];
        if (mapped_a != std::numeric_limits<std::uint32_t>::max() && mapped_b != std::numeric_limits<std::uint32_t>::max()) {
            if (!context->edge_lookup.contains(detail::edgeKey(mapped_a, mapped_b))) {
                debugLog(
                    "search prune: mapped edge missing for local edge " + std::to_string(edge_index) +
                    " candidate vertices (" + std::to_string(mapped_a) + ", " + std::to_string(mapped_b) + ")"
                );
                return;
            }
            continue;
        }
        if (mapped_a == std::numeric_limits<std::uint32_t>::max() && mapped_b == std::numeric_limits<std::uint32_t>::max()) {
            continue;
        }

        const bool expand_from_a = mapped_a != std::numeric_limits<std::uint32_t>::max();
        const std::uint32_t known_vertex = expand_from_a ? mapped_a : mapped_b;
        const std::uint32_t unknown_local = expand_from_a ? edge.b : edge.a;
        std::vector<std::uint32_t> current_options;
        for (const std::uint32_t candidate_edge : context->incident_edges[known_vertex]) {
            const std::uint32_t candidate_vertex = otherVertex(context->all_edges[candidate_edge], known_vertex);
            if (candidate_vertex >= used_vertices->size() ||
                (*used_vertices)[candidate_vertex] != 0 ||
                context->incident_edges[candidate_vertex].size() < context->pattern.degrees[unknown_local]) {
                debugLog(
                    "search reject option: local vertex " + std::to_string(unknown_local) +
                    " candidate vertex " + std::to_string(candidate_vertex) +
                    " basic constraints failed"
                );
                continue;
            }
            bool geometry_mismatch = false;
            for (std::uint32_t local_vertex = 0; local_vertex < vertex_map->size(); ++local_vertex) {
                const std::uint32_t mapped_vertex = (*vertex_map)[local_vertex];
                if (mapped_vertex == std::numeric_limits<std::uint32_t>::max()) {
                    continue;
                }

                const float expected_distance = pairwiseDistance(context->pattern, unknown_local, local_vertex);
                const float actual_distance = edgeLength(context->positions[candidate_vertex], context->positions[mapped_vertex]);
                if (context->params.allow_scaling && !context->params.require_uniform_scaling) {
                    continue;
                }

                const EdgeInfo& anchor = context->pattern.edges[context->pattern.anchor_edge_index];
                const float anchor_scale =
                    context->params.allow_scaling
                        ? context->all_edges[context->edge_lookup.at(detail::edgeKey((*vertex_map)[anchor.a], (*vertex_map)[anchor.b]))].length /
                            std::max(anchor.length, kBasisEpsilon)
                        : 1.0F;
                if (!compatibleLength(
                        expected_distance * anchor_scale,
                        actual_distance,
                        relativeToleranceFraction(context->params)
                    )) {
                    debugLog(
                        "search reject option: pair mismatch unknown_local=" + std::to_string(unknown_local) +
                        " against local=" + std::to_string(local_vertex) +
                        " expected=" + std::to_string(expected_distance * anchor_scale) +
                        " actual=" + std::to_string(actual_distance)
                    );
                    geometry_mismatch = true;
                    break;
                }
            }
            if (geometry_mismatch) {
                continue;
            }
            if (!context->params.allow_scaling || context->params.require_uniform_scaling) {
                const EdgeInfo& anchor = context->pattern.edges[context->pattern.anchor_edge_index];
                const float anchor_scale = context->params.allow_scaling
                    ? context->all_edges[context->edge_lookup.at(detail::edgeKey((*vertex_map)[anchor.a], (*vertex_map)[anchor.b]))].length /
                        std::max(anchor.length, kBasisEpsilon)
                    : 1.0F;
                if (!compatibleLength(
                        edge.length * anchor_scale,
                        context->all_edges[candidate_edge].length,
                        relativeToleranceFraction(context->params)
                    )) {
                    debugLog(
                        "search reject option: edge length mismatch local edge=" + std::to_string(edge_index) +
                        " candidate edge=" + std::to_string(candidate_edge) +
                        " expected=" + std::to_string(edge.length * anchor_scale) +
                        " actual=" + std::to_string(context->all_edges[candidate_edge].length)
                    );
                    continue;
                }
            }
            current_options.push_back(candidate_vertex);
        }
        if (!best_edge.has_value() || current_options.size() < options.size()) {
            best_edge = edge_index;
            options = std::move(current_options);
        }
    }

    if (!best_edge.has_value()) {
        if (std::find(vertex_map->begin(), vertex_map->end(), std::numeric_limits<std::uint32_t>::max()) != vertex_map->end()) {
            return;
        }
        std::vector<std::uint32_t> matched_edges;
        if (validateMatch(*context, *vertex_map, &matched_edges) &&
            matched_edges != context->current_selection &&
            context->accepted.emplace(matched_edges).second) {
            context->matches.push_back(std::move(matched_edges));
        }
        return;
    }

    const EdgeInfo& edge = context->pattern.edges[*best_edge];
    const bool expand_from_a = (*vertex_map)[edge.a] != std::numeric_limits<std::uint32_t>::max();
    const std::uint32_t unknown_local = expand_from_a ? edge.b : edge.a;
    debugLog(
        "search branch " + std::to_string(++context->branch_counter) +
        ": local edge=" + std::to_string(*best_edge) +
        " assigning local vertex=" + std::to_string(unknown_local) +
        " options=" + formatIndices(options)
    );
    for (const std::uint32_t candidate_vertex : options) {
        (*vertex_map)[unknown_local] = candidate_vertex;
        (*used_vertices)[candidate_vertex] = 1;
        debugLog(
            "search try: local vertex " + std::to_string(unknown_local) +
            " -> candidate vertex " + std::to_string(candidate_vertex)
        );
        searchMatches(context, vertex_map, used_vertices);
        (*used_vertices)[candidate_vertex] = 0;
        (*vertex_map)[unknown_local] = std::numeric_limits<std::uint32_t>::max();
    }
}

}  // namespace

SelectSimilarResult evaluateSelectSimilar(
    const std::vector<mesh::Vec3>& positions,
    std::span<const SimilarityEdge> edges,
    const mesh::EntitySelection& current_selection,
    const SelectSimilarParams& params
) {
    SelectSimilarResult result;
    result.selection = current_selection;
    debugLog("============================================================");
    debugLog(
        "evaluate start edges_in_workspace=" + std::to_string(edges.size()) +
        " selected_edges=" + std::to_string(current_selection.edge_indices.size()) +
        " allow_rot=(" + std::to_string(params.allow_rotation_x ? 1 : 0) + "," +
        std::to_string(params.allow_rotation_y ? 1 : 0) + "," +
        std::to_string(params.allow_rotation_z ? 1 : 0) + ")" +
        " allow_scaling=" + std::to_string(params.allow_scaling ? 1 : 0) +
        " require_uniform_scaling=" + std::to_string(params.require_uniform_scaling ? 1 : 0) +
        " tolerance_percent=" + std::to_string(params.tolerance)
    );

    std::vector<std::uint32_t> selected_edges = current_selection.edge_indices;
    sortAndUnique(&selected_edges);
    if (selected_edges.empty()) {
        result.unavailable_reasons.push_back("Select at least one edge.");
        return result;
    }
    if (selected_edges.size() > kMaxSelectedEdges) {
        result.unavailable_reasons.push_back("Select Similar currently supports up to 12 edges.");
        return result;
    }

    const std::vector<EdgeInfo> all_edges = buildEdgeInfo(positions, edges);
    debugLogEdgeInfo(positions, all_edges, selected_edges, "selected edges");
    for (const std::uint32_t edge_index : selected_edges) {
        if (edge_index >= all_edges.size()) {
            result.unavailable_reasons.push_back("The current edge selection is no longer valid.");
            debugLog("evaluate abort: selected edge index out of range");
            return result;
        }
    }

    if (selected_edges.size() == 1U) {
        const float target_length = all_edges[selected_edges.front()].length;
        const float tolerance_fraction = relativeToleranceFraction(params);
        for (std::uint32_t edge_index = 0; edge_index < all_edges.size(); ++edge_index) {
            const bool is_match =
                !contains(selected_edges, edge_index) &&
                compatibleLength(target_length, all_edges[edge_index].length, tolerance_fraction);
            debugLog(
                "single-edge candidate edge=" + std::to_string(edge_index) +
                " target_length=" + std::to_string(target_length) +
                " candidate_length=" + std::to_string(all_edges[edge_index].length) +
                " match=" + std::to_string(is_match ? 1 : 0)
            );
            if (is_match) {
                result.preview_edge_indices.push_back(edge_index);
            }
        }
        sortAndUnique(&result.preview_edge_indices);
        result.selection.edge_indices.insert(
            result.selection.edge_indices.end(),
            result.preview_edge_indices.begin(),
            result.preview_edge_indices.end()
        );
        sortAndUnique(&result.selection.edge_indices);
        result.match_count = result.preview_edge_indices.size();
        result.available = !result.preview_edge_indices.empty();
        if (!result.available) {
            result.unavailable_reasons.push_back("No edges with the same length were found.");
        }
        return result;
    }

    const std::optional<Pattern> pattern = buildPattern(positions, all_edges, selected_edges);
    if (!pattern.has_value()) {
        result.unavailable_reasons.push_back("Select Similar currently requires one connected group of edges.");
        debugLog("evaluate abort: failed to build connected pattern");
        return result;
    }
    debugLogPattern(positions, *pattern);

    const auto incident_edges = buildIncidentEdges(positions.size(), all_edges);
    const auto edge_lookup = buildEdgeLookup(all_edges);
    const float tolerance_fraction = relativeToleranceFraction(params);
    MatchContext context{
        .positions = positions,
        .all_edges = all_edges,
        .incident_edges = incident_edges,
        .edge_lookup = edge_lookup,
        .pattern = *pattern,
        .params = params,
        .current_selection = selected_edges,
    };

    const EdgeInfo& anchor = pattern->edges[pattern->anchor_edge_index];
    for (std::uint32_t edge_index = 0; edge_index < all_edges.size(); ++edge_index) {
        if ((!params.allow_scaling || params.require_uniform_scaling) &&
            !compatibleLength(anchor.length, all_edges[edge_index].length, tolerance_fraction) &&
            !params.allow_scaling) {
            debugLog(
                "anchor reject edge=" + std::to_string(edge_index) +
                " candidate_length=" + std::to_string(all_edges[edge_index].length) +
                " anchor_length=" + std::to_string(anchor.length)
            );
            continue;
        }
        for (int orientation = 0; orientation < 2; ++orientation) {
            std::vector<std::uint32_t> vertex_map(pattern->global_vertices.size(), std::numeric_limits<std::uint32_t>::max());
            std::vector<unsigned char> used_vertices(positions.size(), 0);
            const std::uint32_t first_vertex = orientation == 0 ? all_edges[edge_index].a : all_edges[edge_index].b;
            const std::uint32_t second_vertex = orientation == 0 ? all_edges[edge_index].b : all_edges[edge_index].a;
            vertex_map[anchor.a] = first_vertex;
            vertex_map[anchor.b] = second_vertex;
            used_vertices[first_vertex] = 1;
            used_vertices[second_vertex] = 1;
            debugLog(
                "anchor try edge=" + std::to_string(edge_index) +
                " orientation=" + std::to_string(orientation) +
                " local anchor (" + std::to_string(anchor.a) + "," + std::to_string(anchor.b) + ")" +
                " -> candidate (" + std::to_string(first_vertex) + "," + std::to_string(second_vertex) + ")"
            );
            if (incident_edges[first_vertex].size() < pattern->degrees[anchor.a] ||
                incident_edges[second_vertex].size() < pattern->degrees[anchor.b]) {
                debugLog("anchor reject: degree mismatch");
                continue;
            }
            context.recursion_budget = 20000;
            searchMatches(&context, &vertex_map, &used_vertices);
        }
    }

    for (const std::vector<std::uint32_t>& match : context.matches) {
        debugLog("final match group=" + formatIndices(match));
        ++result.match_count;
        for (const std::uint32_t edge_index : match) {
            if (!contains(selected_edges, edge_index)) {
                result.preview_edge_indices.push_back(edge_index);
            }
        }
    }
    sortAndUnique(&result.preview_edge_indices);
    result.selection.edge_indices.insert(
        result.selection.edge_indices.end(),
        result.preview_edge_indices.begin(),
        result.preview_edge_indices.end()
    );
    sortAndUnique(&result.selection.edge_indices);
    result.available = !result.preview_edge_indices.empty();
    if (!result.available) {
        result.unavailable_reasons.push_back("No similar edge groups were found.");
        debugLog("evaluate result: no similar groups found");
    } else {
        debugLog(
            "evaluate result: match_count=" + std::to_string(result.match_count) +
            " preview_edges=" + formatIndices(result.preview_edge_indices)
        );
    }
    debugLog("============================================================");
    return result;
}

}  // namespace meshtools::render
