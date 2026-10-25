#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include "ghost/animation/rig.hpp"
#include "ghost/core/combat_state.hpp"

namespace ghost::gait
{
struct WeaponBounds
{
    Vec3 center, extent;
};
inline bool heavy_weapon(unsigned id)
{
    return id == 5 || id == 6 || id == 7;
}
inline bool long_gun(unsigned id)
{
    return id == 1 || id == 3;
}
inline bool fixed_firearm_hold(unsigned id)
{
    return id == static_cast<unsigned>(WeaponId::PM) ||
           id == static_cast<unsigned>(WeaponId::Electro) ||
           id == static_cast<unsigned>(WeaponId::Dominator) ||
           id == static_cast<unsigned>(WeaponId::Railgun);
}
inline float shotgun_pitch(float view, float aiming)
{
    // Looking at the floor must not turn the whole carrier vertically between
    // the knees. Keep a lower ready pose, then allow more travel when sighted.
    const float down = 32 + 10 * aiming, up = 38 + 4 * aiming;
    return std::clamp(view, -down, up);
}
inline Vec3 support_grip(const CombatState& equipment)
{
    return {equipment.grip[7], equipment.grip[8], equipment.grip[9]};
}
inline Vec3 rifle_butt_in_hand(const WeaponBounds& bounds, const CombatState& equipment)
{
    const auto root =
        normalized({equipment.grip[3], equipment.grip[4], equipment.grip[5], equipment.grip[6]});
    const auto backwards = rotate(inverse(root), {-1, 0, 0});
    const std::array<float, 3> direction{backwards.x, backwards.y, backwards.z};
    const auto axis =
        static_cast<int>(std::max_element(direction.begin(), direction.end(), [](float a, float b)
                                          { return std::abs(a) < std::abs(b); }) -
                         direction.begin());
    auto point = bounds.center;
    const std::array<float, 3> extent{bounds.extent.x, bounds.extent.y, bounds.extent.z};
    const float offset = std::copysign(extent[axis], direction[axis]);
    if (axis == 0)
        point.x += offset;
    if (axis == 1)
        point.y += offset;
    if (axis == 2)
        point.z += offset;
    return rotate(root, point) + Vec3{equipment.grip[0], equipment.grip[1], equipment.grip[2]};
}
inline Quat comfortable_upper_rotation(Quat full, Vec3 reference, Vec3 current)
{
    const auto swing = from_to(reference, current);
    const auto axis = unit(current);
    const auto turn = normalized(full * inverse(swing));
    const float axial = dot(Vec3{turn.x, turn.y, turn.z}, axis);
    const float angle = std::remainder(2 * std::atan2(axial, turn.w), 2 * pi);
    return axis_angle(axis, std::clamp(angle, -.75f, .75f)) * swing;
}
} // namespace ghost::gait
