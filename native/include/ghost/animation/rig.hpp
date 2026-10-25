#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include "ghost/animation/math.hpp"
#include <array>
#include <optional>

namespace ghost::gait
{
enum Bone : std::size_t
{
    Pelvis,
    Spine1,
    Spine2,
    Spine3,
    Neck,
    Head,
    ClavicleL,
    UpperArmL,
    LowerArmL,
    HandL,
    ClavicleR,
    UpperArmR,
    LowerArmR,
    HandR,
    ThighL,
    CalfL,
    FootL,
    BallL,
    ThighR,
    CalfR,
    FootR,
    BallR,
    Count
};
inline constexpr std::array<const wchar_t*, Count> bone_names{
    L"pelvis",     L"spine_01",   L"spine_02",   L"spine_03", L"neck_01",    L"head",
    L"clavicle_l", L"upperarm_l", L"lowerarm_l", L"hand_l",   L"clavicle_r", L"upperarm_r",
    L"lowerarm_r", L"hand_r",     L"thigh_l",    L"calf_l",   L"foot_l",     L"ball_l",
    L"thigh_r",    L"calf_r",     L"foot_r",     L"ball_r"};
struct BonePose
{
    Vec3 p;
    Quat q;
};
using Pose = std::array<BonePose, Count>;
struct PoseCadence
{
    std::optional<double> scheduled;
    bool due(double now)
    {
        if (!std::isfinite(now))
            throw std::runtime_error("invalid procedural pose clock");
        constexpr double interval = 1.0 / 30.0;
        if (!scheduled || now < *scheduled)
        {
            scheduled = now;
            return true;
        }
        const double periods = std::floor((now - *scheduled) / interval);
        if (periods < 1)
            return false;
        *scheduled += periods * interval;
        return true;
    }
};
struct Rig
{
    Pose rest;
    Vec3 forward, right;
    explicit Rig(const Pose& reference) : rest(reference)
    {
        for (auto& bone : rest)
        {
            if (!valid_vector(bone.p) || length(bone.p) > 1000)
                throw std::runtime_error("invalid procedural reference pose");
            bone.q = normalized(bone.q);
        }
        auto toes = (rest[BallL].p - rest[FootL].p) + (rest[BallR].p - rest[FootR].p);
        toes.z = 0;
        forward = unit(toes);
        right = unit(cross({0, 0, 1}, forward));
        for (const auto chain :
             {std::array{ThighL, CalfL, FootL}, std::array{ThighR, CalfR, FootR},
              std::array{UpperArmL, LowerArmL, HandL}, std::array{UpperArmR, LowerArmR, HandR}})
            for (int i = 0; i < 2; ++i)
            {
                const float n = length(rest[chain[i + 1]].p - rest[chain[i]].p);
                if (n < 5 || n > 150)
                    throw std::runtime_error("unsupported procedural limb proportions");
            }
    }
};
struct Limb
{
    Vec3 joint, end;
};
inline Limb solve_limb(Vec3 start, Vec3 target, Vec3 pole, float upper, float lower)
{
    if (!valid_vector(start) || !valid_vector(target) || !valid_vector(pole) ||
        !std::isfinite(upper) || !std::isfinite(lower) || upper <= 0.001f || lower <= 0.001f)
        throw std::runtime_error("invalid procedural IK input");
    Vec3 delta = target - start;
    const float distance = length(delta);
    const Vec3 direction = distance > 1e-5f ? delta * (1.0f / distance) : Vec3{0, 0, -1};
    const float d = std::clamp(distance, std::abs(upper - lower) + 0.001f, upper + lower - 0.001f);
    const float along = (upper * upper - lower * lower + d * d) / (2 * d);
    const float height = std::sqrt(std::max(0.0f, upper * upper - along * along));
    Vec3 bend = pole - direction * dot(pole, direction);
    if (length(bend) < 1e-4f)
        bend = cross(direction, std::abs(direction.x) < 0.8f ? Vec3{1, 0, 0} : Vec3{0, 1, 0});
    return {start + direction * along + unit(bend) * height, start + direction * d};
}
} // namespace ghost::gait
