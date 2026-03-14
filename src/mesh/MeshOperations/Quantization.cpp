#include "meshtools/mesh/MeshOperations/Detail.h"

#include <cmath>

namespace meshtools::mesh::operations::detail {
namespace {

constexpr double kWeldPrecision = 1000000.0;

}  // namespace

std::size_t QuantizedPositionKeyHash::operator()(const QuantizedPositionKey& key) const noexcept {
    const std::size_t hx = std::hash<std::int64_t>{}(key.x);
    const std::size_t hy = std::hash<std::int64_t>{}(key.y);
    const std::size_t hz = std::hash<std::int64_t>{}(key.z);
    return hx ^ (hy << 1U) ^ (hz << 2U);
}

std::size_t QuantizedEdgeKeyHash::operator()(const QuantizedEdgeKey& key) const noexcept {
    const std::size_t ha = QuantizedPositionKeyHash{}(key.a);
    const std::size_t hb = QuantizedPositionKeyHash{}(key.b);
    return ha ^ (hb << 1U);
}

bool lessThan(const QuantizedPositionKey& left, const QuantizedPositionKey& right) {
    if (left.x != right.x) {
        return left.x < right.x;
    }
    if (left.y != right.y) {
        return left.y < right.y;
    }
    return left.z < right.z;
}

QuantizedPositionKey makeQuantizedPositionKey(const Vec3& position) {
    return QuantizedPositionKey{
        .x = static_cast<std::int64_t>(std::llround(static_cast<double>(position.x) * kWeldPrecision)),
        .y = static_cast<std::int64_t>(std::llround(static_cast<double>(position.y) * kWeldPrecision)),
        .z = static_cast<std::int64_t>(std::llround(static_cast<double>(position.z) * kWeldPrecision)),
    };
}

QuantizedEdgeKey makeQuantizedEdgeKey(QuantizedPositionKey first, QuantizedPositionKey second) {
    if (lessThan(second, first)) {
        std::swap(first, second);
    }

    return QuantizedEdgeKey{
        .a = first,
        .b = second,
    };
}

}  // namespace meshtools::mesh::operations::detail
