#pragma once
#include "ghost/animation/rig.hpp"
#include <algorithm>
#include <cmath>

namespace ghost::gait
{
// Limits are presentation tolerances, derived from the calibrated limb lengths.
// Project targets before IK so a shorter rig cannot overfold to satisfy an FP grip.
struct ArmReach
{
    float upper, lower, minimum, maximum;
    ArmReach(float a, float b) : upper(a), lower(b)
    {
        if (!std::isfinite(a) || !std::isfinite(b) || a <= 0 || b <= 0)
            throw std::runtime_error("invalid arm reach lengths");
        constexpr float maximum_bend = 145.0f * pi / 180;
        minimum = std::sqrt(a * a + b * b + 2 * a * b * std::cos(maximum_bend));
        maximum = .97f * (a + b);
    }
    Vec3 project(Vec3 shoulder, Vec3 target, Vec3 fallback) const
    {
        const auto offset = target - shoulder;
        const float distance = length(offset);
        const auto direction = distance > 1e-5f ? offset * (1 / distance) : unit(fallback);
        return shoulder + direction * std::clamp(distance, minimum, maximum);
    }
    float error(Vec3 shoulder, Vec3 target) const
    {
        const float distance = length(target - shoulder);
        return std::max({0.0f, minimum - distance, distance - maximum});
    }
};

inline Quat wrist_cone(Quat frame, Vec3 forearm, Vec3 fallback, float maximum_angle)
{
    forearm = unit(forearm);
    const auto fingers = rotate(frame, {1, 0, 0});
    const float cosine = std::clamp(dot(fingers, forearm), -1.0f, 1.0f);
    const float minimum_cosine = std::cos(maximum_angle);
    if (cosine >= minimum_cosine)
        return frame;
    auto tangent = fingers - forearm * cosine;
    if (length(tangent) < 1e-4f)
    {
        tangent = fallback - forearm * dot(fallback, forearm);
        if (length(tangent) < 1e-4f)
            tangent = cross(forearm, std::abs(forearm.x) < .8f ? Vec3{1, 0, 0} : Vec3{0, 1, 0});
    }
    const auto limited = forearm * minimum_cosine + unit(tangent) * std::sin(maximum_angle);
    return from_to(fingers, limited) * frame;
}
} // namespace ghost::gait
