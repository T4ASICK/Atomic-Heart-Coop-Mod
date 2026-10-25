#pragma once
#include <cmath>
#include "ghost/core/vector.hpp"
#include "ghost/core/combat_state.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace ghost
{
// Wire positions are visual anchors 90 cm above the capsule base. Unlike the
// capsule center this stays at a constant height when the capsule crouches.
// Existing FeetOffset=-90 then places the mesh origin at that base. Custom
// offsets remain an additional visual adjustment; gameplay locations are untouched.
inline Vec3 character_visual_anchor(Vec3 center, float half_height)
{
    if (std::isfinite(half_height) && half_height >= 20 && half_height <= 200)
        center.z += 90 - half_height;
    return center;
}

struct State
{
    std::string id, name, session, world;
    std::uint64_t seq{};
    Vec3 position{}, velocity{};
    float yaw{};
    CombatState combat{};
};
struct Roster
{
    std::string session;
    std::uint64_t seq{};
    std::vector<State> players;
};
} // namespace ghost
