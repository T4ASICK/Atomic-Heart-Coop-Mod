#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include "ghost/core/state.hpp"
#include <array>

namespace ghost
{
inline bool finite(Vec3 p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
inline std::array<Vec3, 4> arrival_candidates(const State& peer, float radius)
{
    const float yaw = peer.yaw * 0.0174532925f;
    const float distance = std::max(160.0f, 2 * radius + 60.0f);
    const Vec3 right{-std::sin(yaw) * distance, std::cos(yaw) * distance, 0};
    const Vec3 back{-std::cos(yaw) * distance, -std::sin(yaw) * distance, 0};
    return {peer.position + right, peer.position - right, peer.position + back,
            peer.position - back};
}
inline std::optional<Vec3> grounded_arrival(Vec3 point, Vec3 normal, bool blocking, bool overlap,
                                            float half_height)
{
    if (!blocking || overlap || !finite(point) || !finite(normal) || normal.z < 0.7f ||
        !std::isfinite(half_height) || half_height < 20 || half_height > 200)
        return {};
    return point + Vec3{0, 0, half_height + 4};
}
class TeleportCooldown
{
    double next{};

public:
    bool consume(double now)
    {
        if (now < next)
            return false;
        next = now + 2.0;
        return true;
    }
};
} // namespace ghost
