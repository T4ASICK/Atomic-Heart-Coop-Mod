#pragma once
#include <array>
#include <cmath>
#include <stdexcept>
#include "ghost/animation/rig.hpp"

namespace ghost::gait
{
// Calibrate from the mesh, including handedness; do not assume finger Euler axes.
struct HandRig
{
    std::array<BonePose, 15> relaxed_local;
    std::array<Quat, 15> open_local, grip_local;
    BonePose reference;
    Vec3 fingers, thumb;
    HandRig(BonePose hand, std::array<BonePose, 15> rest, bool left) : reference(hand)
    {
        reference.q = normalized(reference.q);
        for (auto& bone : rest)
        {
            if (!valid_vector(bone.p) || length(bone.p) > 1000)
                throw std::runtime_error("invalid procedural finger reference");
            bone.q = normalized(bone.q);
        }
        fingers = unit(rest[3].p - hand.p);  // middle metacarpal direction
        thumb = unit(rest[0].p - rest[9].p); // pinky toward index/thumb side
        const auto palm = unit(cross(fingers, thumb)) * (left ? -1.0f : 1.0f);
        for (int finger = 0; finger < 5; ++finger)
            for (int joint = 0; joint < 3; ++joint)
            {
                const int i = finger * 3 + joint;
                const auto& parent = joint ? rest[i - 1] : hand;
                const auto& current = rest[i];
                const auto direction =
                    joint < 2 ? rest[i + 1].p - current.p : current.p - rest[i - 1].p;
                const auto hinge = unit(cross(unit(direction), palm));
                const auto local_hinge = rotate(inverse(current.q), hinge);
                const float angle = finger == 4 ? (joint == 0 ? 0.12f : 0.20f)
                                                : (joint == 0   ? 0.35f
                                                   : joint == 1 ? 0.55f
                                                                : 0.30f);
                auto local_q = inverse(parent.q) * current.q;
                // Bring the open reference spread modestly toward the middle finger.
                if (joint == 0 && finger < 4)
                {
                    const auto toward = unit(unit(direction) * 0.75f + fingers * 0.25f);
                    local_q = inverse(parent.q) * from_to(direction, toward) * current.q;
                }
                relaxed_local[i] = {rotate(inverse(parent.q), current.p - parent.p),
                                    local_q * axis_angle(local_hinge, angle)};
                open_local[i] = local_q;
                const float grasp = finger == 4 ? (joint == 0 ? 0.35f : 0.65f)
                                                : (joint == 0   ? (finger == 0 ? 0.55f : 0.85f)
                                                   : joint == 1 ? 1.05f
                                                                : 0.65f);
                grip_local[i] = local_q * axis_angle(local_hinge, grasp);
            }
        // Oppose the thumb to the curled fingers. Flexion alone leaves it
        // abducted beside the fist, even when the other fingers hold a shaft.
        const auto canonical = orient_axes({1, 0, 0}, {0, 1, 0});
        const auto closed = evaluate({{}, canonical}, 1);
        const auto contact = grip_center() + Vec3{0, grip_span() * .4f, 0};
        const auto opposition = from_to(closed[14].p - closed[12].p, contact - closed[12].p);
        grip_local[12] = inverse(canonical) * opposition * canonical * grip_local[12];
    }
    Quat orientation(Vec3 elbow, Vec3 wrist, Vec3 forward) const
    {
        const auto along = unit(wrist - elbow);
        return orient_axes(along, forward);
    }
    Quat orient_axes(Vec3 finger_axis, Vec3 thumb_axis) const
    {
        const auto along = unit(finger_axis);
        const auto aim = from_to(fingers, along);
        const auto original = unit(rotate(aim, thumb) - along * dot(rotate(aim, thumb), along));
        auto desired = thumb_axis - along * dot(thumb_axis, along);
        if (length(desired) < 1e-4f)
            desired = original;
        desired = unit(desired);
        const float roll = std::atan2(dot(along, cross(original, desired)), dot(original, desired));
        return axis_angle(along, roll) * aim * reference.q;
    }
    std::array<BonePose, 15> evaluate(BonePose hand, float grasp = 0) const
    {
        std::array<BonePose, 15> out;
        for (int i = 0; i < 15; ++i)
        {
            const auto& parent = i % 3 ? out[i - 1] : hand;
            const auto q = blend_rotation(
                relaxed_local[i].q, grasp < 0 ? open_local[i] : grip_local[i], std::abs(grasp));
            out[i] = {parent.p + rotate(parent.q, relaxed_local[i].p), parent.q * q};
        }
        return out;
    }
    Vec3 grip_center(float grasp = 1) const
    {
        // Work in the anatomical finger/thumb basis, not a bone's Euler axes.
        const auto closed = evaluate({{}, orient_axes({1, 0, 0}, {0, 1, 0})}, grasp);
        Vec3 center{};
        unsigned samples{};
        for (int finger = 1; finger < 4; ++finger)
        {
            const auto a = closed[finger * 3].p, b = closed[finger * 3 + 1].p,
                       c = closed[finger * 3 + 2].p;
            const float x = b.x - a.x, z = b.z - a.z, u = c.x - a.x, v = c.z - a.z;
            const float determinant = 2 * (x * v - z * u);
            if (std::abs(determinant) < 1e-5f)
                continue;
            const float ab = x * x + z * z, ac = u * u + v * v;
            center = center + Vec3{a.x + (ab * v - ac * z) / determinant, (a.y + b.y + c.y) / 3,
                                   a.z + (x * ac - u * ab) / determinant};
            ++samples;
        }
        if (!samples)
            throw std::runtime_error("cannot derive a contact from straight fingers");
        return center * (1.0f / samples);
    }
    float grip_span() const
    {
        const auto closed = evaluate({{}, orient_axes({1, 0, 0}, {0, 1, 0})}, 1);
        return std::abs(closed[0].p.y - closed[9].p.y);
    }
};
} // namespace ghost::gait
