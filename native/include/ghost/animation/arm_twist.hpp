#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include "ghost/animation/rig.hpp"

namespace ghost::gait
{
inline constexpr std::array<const wchar_t*, 6> twist_names{
    L"upperarm_twist_01_l", L"lowerarm_twist_01_l", L"lowerarm_twist_02_l",
    L"upperarm_twist_01_r", L"lowerarm_twist_01_r", L"lowerarm_twist_02_r"};

inline float axial_twist(Quat delta, Vec3 axis)
{
    axis = unit(axis);
    delta = normalized(delta);
    const float parallel = dot({delta.x, delta.y, delta.z}, axis);
    if (parallel * parallel + delta.w * delta.w < 1e-8f)
        return 0;
    return std::clamp(std::remainder(2 * std::atan2(parallel, delta.w), 2 * pi), -2.0943951f,
                      2.0943951f);
}

struct ArmTwist
{
    Pose rest;
    std::array<BonePose, 6> reference;
    ArmTwist(const Rig& rig, std::array<BonePose, 6> transforms)
        : rest(rig.rest), reference(transforms)
    {
        for (auto& t : reference)
        {
            if (!valid_vector(t.p) || length(t.p) > 1000)
                throw std::runtime_error("invalid twist reference");
            t.q = normalized(t.q);
        }
    }
    std::array<BonePose, 6> apply(Pose& pose) const
    {
        std::array<BonePose, 6> out;
        for (int side = 0; side < 2; ++side)
        {
            const auto upper = side == 0 ? UpperArmL : UpperArmR,
                       lower = side == 0 ? LowerArmL : LowerArmR;
            const auto hand = side == 0 ? HandL : HandR;
            const auto upper_delta = pose[upper].q * inverse(rest[upper].q);
            const auto lower_delta = pose[lower].q * inverse(rest[lower].q);
            const auto predicted = lower_delta * rest[hand].q;
            const auto axis = unit(pose[hand].p - pose[lower].p);
            const float turn = axial_twist(pose[hand].q * inverse(predicted), axis);
            // Pronation rolls the forearm gradually. Keep the wrist's final grip
            // orientation and all joint positions unchanged.
            pose[lower].q = axis_angle(axis, turn * 0.25f) * pose[lower].q;
            for (int piece = 0; piece < 3; ++piece)
            {
                const int i = side * 3 + piece;
                const auto parent = piece == 0 ? upper : lower;
                const auto delta = piece == 0 ? upper_delta : lower_delta;
                const float fraction = piece == 0 ? 0 : piece == 1 ? 0.5f : 0.8f;
                out[i] = {pose[parent].p + rotate(delta, reference[i].p - rest[parent].p),
                          axis_angle(axis, turn * fraction) * delta * reference[i].q};
            }
        }
        return out;
    }
};
} // namespace ghost::gait
