#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include "ghost/animation/rig.hpp"

namespace ghost::gait
{
// A conservative clothing envelope fitted from the shoulder span, pelvis and
// spine. This uses reference proportions, never a supplied mesh or game path.
struct BodyClearance
{
    const Rig& rig;
    const Pose& pose;
    float scale;
    Vec3 forward, right;
    BodyClearance(const Rig& r, const Pose& p)
        : rig(r), pose(p), scale(length(r.rest[UpperArmL].p - r.rest[UpperArmR].p) / 44.4f)
    {
        const auto turn = p[Spine2].q * inverse(r.rest[Spine2].q);
        forward = rotate(turn, r.forward);
        right = rotate(turn, r.right);
    }

    float front() const
    {
        return dot(pose[Spine2].p, forward) + 24 * scale;
    }
    Vec3 wrist(Vec3 point) const
    {
        if (point.z > pose[Pelvis].p.z - 12 * scale && point.z < pose[Head].p.z + 13 * scale)
        {
            const float lateral = dot(point - pose[Spine2].p, right) / (27 * scale);
            const float extent = 24 * scale * std::sqrt(std::max(0.0f, 1 - lateral * lateral));
            if (std::abs(lateral) < 1)
                point = point + forward * std::max(0.0f, dot(pose[Spine2].p, forward) + extent -
                                                             dot(point, forward));
        }
        return point;
    }
    float penetration(Vec3 point, float radius) const
    {
        const float bottom = pose[Pelvis].p.z - 7 * scale, top = pose[Neck].p.z + scale;
        float depth = 0;
        if (point.z > bottom && point.z < top)
        {
            const float t = std::clamp((point.z - bottom) / (top - bottom), 0.0f, 1.0f);
            const auto center = pose[Spine1].p * (1 - t) + pose[Spine3].p * t;
            const float x = dot(point - center, forward) / ((18 - 2 * t) * scale + radius);
            const float y = dot(point - center, right) / ((21 - 3 * t) * scale + radius);
            depth = std::max(0.0f, 1 - std::sqrt(x * x + y * y)) * 20 * scale;
        }
        const auto head = pose[Head].p;
        depth = std::max(depth, std::max(0.0f, 14 * scale + radius - length(point - head)));
        return depth;
    }
    Limb arm(Vec3 start, Vec3 target, float upper, float lower, bool left, Vec3 fingers, Vec3 pole,
             const std::optional<Vec3>& previous, float dt, bool lower_elbow = false,
             float pole_speed = 8) const
    {
        const auto initial = solve_limb(start, target, pole, upper, lower);
        const auto along = unit(initial.end - start);
        const auto center = start + along * dot(initial.joint - start, along);
        const float radius = length(initial.joint - center);
        if (radius < 0.001f)
            return initial;
        const auto x = unit(initial.joint - center), y = unit(cross(along, x));
        auto joint = [&](float angle)
        { return center + (x * std::cos(angle) + y * std::sin(angle)) * radius; };
        auto score = [&](Vec3 elbow)
        {
            float cost = 0;
            for (float t : {0.45f, 0.65f, 0.85f, 1.0f})
            {
                const float d = penetration(start * (1 - t) + elbow * t, 4.8f * scale);
                cost += d * d * 2000;
            }
            for (float t : {0.2f, 0.4f, 0.6f, 0.8f, 1.0f})
            {
                const float d = penetration(elbow * (1 - t) + initial.end * t, 3.8f * scale);
                cost += d * d * 2000;
            }
            // Prefer a straight continuation into the palm, with an outward,
            // lowered elbow. Continuity selects a stable branch of the IK circle.
            cost += 1800 * (1 - dot(unit(initial.end - elbow), fingers));
            const float raised = std::max(0.0f, elbow.z - start.z + (lower_elbow ? 8 : -8) * scale);
            cost += raised * raised * (lower_elbow ? 35 : 1);
            const float across = dot(elbow - pose[Spine3].p, right) * (left ? -1.0f : 1.0f);
            cost +=
                std::max(0.0f, 17 * scale - across) * std::max(0.0f, 17 * scale - across) * 2000;
            cost += length(elbow - initial.joint) * 0.3f;
            if (previous)
            {
                const float distance = length(elbow - *previous);
                cost += distance * distance * 0.04f;
            }
            return cost;
        };
        float best = 0, best_score = score(initial.joint);
        for (int i = 1; i < 32; ++i)
        {
            const float a = 2 * pi * i / 32, s = score(joint(a));
            if (s < best_score)
            {
                best = a;
                best_score = s;
            }
        }
        float step = 2 * pi / 32;
        for (int iteration = 0; iteration < 5; ++iteration)
        {
            step *= 0.5f;
            for (float candidate : {best - step, best + step})
            {
                const float s = score(joint(candidate));
                if (s < best_score)
                {
                    best = candidate;
                    best_score = s;
                }
            }
        }
        if (previous)
        {
            const float previous_angle =
                std::atan2(dot(*previous - center, y), dot(*previous - center, x));
            float turn = std::remainder(best - previous_angle, 2 * pi);
            const float limit = pole_speed * dt;
            if (std::abs(turn) > limit)
            {
                const float alternate = turn - std::copysign(2 * pi, turn);
                auto route_cost = [&](float arc)
                {
                    float maximum = 0;
                    const int steps = std::max(2, static_cast<int>(std::abs(arc) / .25f));
                    for (int i = 1; i <= steps; ++i)
                        maximum = std::max(maximum, score(joint(previous_angle + arc * i / steps)));
                    return maximum;
                };
                if (route_cost(alternate) + 200 < route_cost(turn))
                    turn = alternate;
                best = previous_angle + std::clamp(turn, -limit, limit);
            }
        }
        return {joint(best), initial.end};
    }
};
} // namespace ghost::gait
