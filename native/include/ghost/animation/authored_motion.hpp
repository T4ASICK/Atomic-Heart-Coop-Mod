#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include "ghost/animation/rig.hpp"
#include "vendor/quaternius/locomotion_data.hpp"

namespace ghost::gait
{
inline authored::Frame blend_motion(const authored::Frame& a, const authored::Frame& b, float t)
{
    authored::Frame out;
    for (std::size_t i = 0; i < Count; ++i)
    {
        out.offset[i] = a.offset[i] * (1 - t) + b.offset[i] * t;
        out.rotation[i] = blend_rotation(a.rotation[i], b.rotation[i], t);
    }
    return out;
}
inline authored::Frame sample_motion(const authored::Clip& clip, double phase)
{
    phase -= std::floor(phase);
    const double f = phase * clip.count;
    const auto i = static_cast<std::size_t>(f) % clip.count;
    return blend_motion(clip.frames[i], clip.frames[(i + 1) % clip.count],
                        static_cast<float>(f - std::floor(f)));
}
inline authored::Frame sample_motion_once(const authored::Clip& clip, float progress)
{
    const float f = std::clamp(progress, 0.0f, 1.0f) * static_cast<float>(clip.count - 1);
    const auto i = static_cast<std::size_t>(f);
    return blend_motion(clip.frames[i], clip.frames[std::min(i + 1, clip.count - 1)],
                        f - static_cast<float>(i));
}
inline float sole_height(const Rig& rig, const Pose& pose, Bone foot)
{
    const auto ball = foot == FootL ? BallL : BallR;
    const auto delta = pose[foot].q * inverse(rig.rest[foot].q);
    const auto heel = rig.forward * -5 + Vec3{0, 0, -rig.rest[foot].p.z};
    const auto toe = rig.rest[ball].p - rig.rest[foot].p + Vec3{0, 0, -rig.rest[ball].p.z};
    return std::min((pose[foot].p + rotate(delta, heel)).z, (pose[foot].p + rotate(delta, toe)).z);
}
inline Pose retarget_motion(const Rig& rig, const authored::Frame& frame, Vec3 direction,
                            float movement, bool& has_support,
                            const std::optional<Quat>& upper_root_rotation = {})
{
    static_assert(Count == 22);
    const float leg = (length(rig.rest[CalfL].p - rig.rest[ThighL].p) +
                       length(rig.rest[FootL].p - rig.rest[CalfL].p) +
                       length(rig.rest[CalfR].p - rig.rest[ThighR].p) +
                       length(rig.rest[FootR].p - rig.rest[CalfR].p)) *
                      0.5f;
    const auto basis = frame_rotation(rig.forward, rig.right);
    auto vector = [&](Vec3 v) { return rig.forward * v.x + rig.right * v.y + Vec3{0, 0, v.z}; };
    auto source = [&](Bone bone) { return vector(authored::reference[bone] + frame.offset[bone]); };
    std::array<Quat, Count> delta;
    for (std::size_t i = 0; i < Count; ++i)
        delta[i] = basis * frame.rotation[i] * inverse(basis);
    constexpr std::array<int, Count> parents{-1, 0,  1,  2, 3,  4,  3,  6, 7,  8,  3,
                                             10, 11, 12, 0, 14, 15, 16, 0, 18, 19, 20};
    Pose out = rig.rest;
    out[Pelvis].p = rig.rest[Pelvis].p + vector(frame.offset[Pelvis]) * leg;
    out[Pelvis].q = delta[Pelvis] * rig.rest[Pelvis].q;
    for (std::size_t i = 1; i < Count; ++i)
    {
        const auto parent = static_cast<std::size_t>(parents[i]);
        // Counteract pelvis roll at the first spine offset as well as the chest
        // rotations. Pelvis and legs retain their complete locomotion cycle.
        const auto parent_delta = i == Spine1 && upper_root_rotation
                                      ? basis * *upper_root_rotation * inverse(basis)
                                      : delta[parent];
        out[i] = {out[parent].p + rotate(parent_delta, rig.rest[i].p - rig.rest[parent].p),
                  delta[i] * rig.rest[i].q};
    }
    const float side = std::abs(dot(direction, rig.right));
    const bool reverse = dot(direction, rig.forward) < -0.25f;
    const float angle = std::atan2(dot(direction, rig.right) * (reverse ? -1.0f : 1.0f),
                                   std::abs(dot(direction, rig.forward)));
    const auto travel = axis_angle({0, 0, 1}, angle);
    has_support = false;
    std::array<bool, 2> contact;
    for (int side_index = 0; side_index < 2; ++side_index)
    {
        const bool left = side_index == 0;
        const auto foot = left ? FootL : FootR, ball = left ? BallL : BallR,
                   thigh = left ? ThighL : ThighR;
        auto neutral = rig.rest[foot].p;
        const float width = (3 + 12 * side * movement) * (left ? -1.0f : 1.0f);
        neutral = neutral + rig.right * (width - dot(neutral - rig.rest[thigh].p, rig.right));
        auto step = vector(frame.offset[foot]) * leg;
        step = rotate(travel, Vec3{step.x * (1 - 0.5f * side), step.y * (1 - 0.5f * side), 0}) +
               Vec3{0, 0, step.z};
        out[foot].p = neutral + step;
        contact[side_index] = std::min(frame.offset[foot].z, frame.offset[ball].z) < 0.018f;
        const float sole = sole_height(rig, out, foot);
        if (contact[side_index] || sole < 0)
            out[foot].p.z -= sole;
        has_support = has_support || contact[side_index];
    }
    // A single pelvis correction fits both legs, preserving the authored rhythm
    // while compensating for different thigh/shin proportions.
    float drop = 0;
    for (int side_index = 0; side_index < 2; ++side_index)
    {
        const auto thigh = side_index == 0 ? ThighL : ThighR,
                   calf = side_index == 0 ? CalfL : CalfR;
        const auto foot = side_index == 0 ? FootL : FootR;
        const auto offset = out[foot].p - out[thigh].p;
        const float reach = 0.995f * (length(rig.rest[calf].p - rig.rest[thigh].p) +
                                      length(rig.rest[foot].p - rig.rest[calf].p));
        const float planar = offset.x * offset.x + offset.y * offset.y;
        const float allowed = std::sqrt(std::max(0.01f, reach * reach - planar));
        drop = std::max(drop, out[thigh].p.z - out[foot].p.z - allowed);
    }
    for (std::size_t i = 0; i < Count; ++i)
        if (i != FootL && i != BallL && i != FootR && i != BallR)
            out[i].p.z -= drop;
    auto limb = [&](Bone a, Bone b, Bone c, Vec3 target, Vec3 pole)
    {
        const auto start = out[a].p;
        const auto result = solve_limb(start, target, pole, length(rig.rest[b].p - rig.rest[a].p),
                                       length(rig.rest[c].p - rig.rest[b].p));
        const auto rotations =
            limb_rotations(rig.rest[b].p - rig.rest[a].p, rig.rest[c].p - rig.rest[b].p,
                           result.joint - start, result.end - result.joint);
        out[a] = {start, rotations[0] * rig.rest[a].q};
        out[b] = {result.joint, rotations[1] * rig.rest[b].q};
        out[c].p = result.end;
    };
    for (int side_index = 0; side_index < 2; ++side_index)
    {
        const bool left = side_index == 0;
        const auto thigh = left ? ThighL : ThighR, calf = left ? CalfL : CalfR,
                   foot = left ? FootL : FootR, ball = left ? BallL : BallR;
        limb(thigh, calf, foot, out[foot].p, source(calf) - source(thigh));
        out[ball] = {out[foot].p + rotate(delta[foot], rig.rest[ball].p - rig.rest[foot].p),
                     delta[foot] * rig.rest[ball].q};
        const auto upper = left ? UpperArmL : UpperArmR, lower = left ? LowerArmL : LowerArmR,
                   hand = left ? HandL : HandR;
        const auto a = unit(source(lower) - source(upper)), b = unit(source(hand) - source(lower));
        const auto elbow = out[upper].p + a * length(rig.rest[lower].p - rig.rest[upper].p);
        const auto wrist = elbow + b * length(rig.rest[hand].p - rig.rest[lower].p);
        const auto rotations = limb_rotations(rig.rest[lower].p - rig.rest[upper].p,
                                              rig.rest[hand].p - rig.rest[lower].p, a, b);
        out[upper].q = rotations[0] * rig.rest[upper].q;
        out[lower] = {elbow, rotations[1] * rig.rest[lower].q};
        out[hand] = {wrist, delta[hand] * rig.rest[hand].q};
    }
    return out;
}
} // namespace ghost::gait
