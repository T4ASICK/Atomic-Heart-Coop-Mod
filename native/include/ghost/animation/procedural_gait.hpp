#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include "ghost/animation/rig.hpp"
#include "ghost/animation/authored_motion.hpp"

namespace ghost::gait
{
struct Step
{
    float travel{}, lift{};
};
inline Step foot_path(float phase, float duty = 0.62f)
{
    phase -= std::floor(phase);
    if (phase < duty)
        return {0.5f - phase / duty, 0};
    const float t = (phase - duty) / (1 - duty);
    const float smooth = t * t * (3 - 2 * t);
    return {-0.5f + smooth, std::sin(pi * t) * std::sin(pi * t)};
}

class Animator
{
    float phase{}, blend{};
    Vec3 direction{1, 0, 0};
    std::optional<double> previous;
    double elapsed{};
    bool support{true};

public:
    float movement_blend() const
    {
        return blend;
    }
    Vec3 movement_direction() const
    {
        return direction;
    }
    bool has_support() const
    {
        return support;
    }
    Pose evaluate(const Rig& rig, Vec3 velocity, double now,
                  const authored::Frame* action = nullptr, float weight = 0,
                  float upper_body_stability = 0, bool fixed_upper_body = false)
    {
        if (!valid_vector(velocity) || !std::isfinite(now))
            throw std::runtime_error("invalid authored gait input");
        const float dt = previous ? static_cast<float>(std::clamp(now - *previous, 0.0, 0.1)) : 0;
        previous = now;
        elapsed += dt;
        const float speed = std::hypot(velocity.x, velocity.y), alpha = 1 - std::exp(-dt / .16f);
        blend += (std::clamp((speed - 10) / 65.0f, 0.0f, 1.0f) - blend) * alpha;
        if (speed > 1)
        {
            const float angle = std::atan2(direction.y, direction.x);
            const auto desired = unit(velocity);
            const float turn = std::remainder(std::atan2(desired.y, desired.x) - angle, 2 * pi);
            direction = {std::cos(angle + alpha * turn), std::sin(angle + alpha * turn), 0};
        }
        const float jog = std::clamp((speed - 140) / 110, 0.0f, 1.0f);
        const float sprint = std::clamp((speed - 270) / 130, 0.0f, 1.0f);
        const float leg = (length(rig.rest[CalfL].p - rig.rest[ThighL].p) +
                           length(rig.rest[FootL].p - rig.rest[CalfL].p) +
                           length(rig.rest[CalfR].p - rig.rest[ThighR].p) +
                           length(rig.rest[FootR].p - rig.rest[CalfR].p)) *
                          .5f;
        const float side = std::abs(dot(direction, rig.right));
        const float walk_distance = authored::walk.speed * authored::walk.duration;
        const float jog_distance = authored::jog.speed * authored::jog.duration * .6f;
        const float sprint_distance = authored::sprint.speed * authored::sprint.duration * .6f;
        const float distance = ((walk_distance * (1 - jog) + jog_distance * jog) * (1 - sprint) +
                                sprint_distance * sprint) *
                               leg * (1 - .5f * side);
        phase = std::fmod(phase + dt * speed / std::max(distance, 1.0f), 1.0f);
        const float sample_phase = dot(direction, rig.forward) < -.25f ? -phase : phase;
        auto walk = sample_motion(authored::walk, sample_phase);
        auto run = sample_motion(authored::jog, sample_phase);
        auto fast = sample_motion(authored::sprint, sample_phase);
        // Shorten the source's fast-run strides to the game's ordinary jogging
        // speed; preserve authored timing and vertical/ankle motion.
        for (auto* frame : {&run, &fast})
            for (const auto bone : {FootL, BallL, FootR, BallR})
            {
                frame->offset[bone].x *= .6f;
                frame->offset[bone].y *= .6f;
            }
        const auto moving = blend_motion(blend_motion(walk, run, jog), fast, sprint);
        const auto idle = sample_motion(authored::idle, elapsed / authored::idle.duration);
        auto frame = blend_motion(idle, moving, blend);
        if (action)
            frame = blend_motion(frame, *action, std::clamp(weight, 0.0f, 1.0f));
        const auto upper_idle = fixed_upper_body ? sample_motion(authored::idle, 0) : idle;
        // A fixed carry uses one reference hold, including the spine anchor.
        // Sampling breathing here still rotates a supposedly motionless arm.
        for (std::size_t bone = Spine1; bone <= HandR; ++bone)
            frame.rotation[bone] = blend_rotation(frame.rotation[bone], upper_idle.rotation[bone],
                                                  std::clamp(upper_body_stability, 0.0f, 1.0f));
        const auto upper_root =
            upper_body_stability > 0
                ? std::optional<Quat>(blend_rotation(
                      frame.rotation[Pelvis], upper_idle.rotation[Pelvis], upper_body_stability))
                : std::nullopt;
        return retarget_motion(rig, frame, direction, blend, support, upper_root);
    }
};
} // namespace ghost::gait
