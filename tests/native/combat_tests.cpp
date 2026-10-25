#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include "ghost/animation/combat_animation.hpp"
#include "fixtures/gait_fixture.hpp"
#include "ghost/visual/weapon_assets.hpp"
#include <fstream>
#include <iostream>

using namespace ghost;
using namespace ghost::gait;
void require(bool value, const char* label)
{
    if (!value)
        throw std::runtime_error(label);
}
CombatState calibrated_rifle()
{
    CombatState state;
    state.weapon = 1;
    state.calibrated = true;
    const auto right = frame_rotation({0, 0, -1}, {1, 0, 0}),
               left = frame_rotation({0, 1, 0}, {1, 0, 0});
    state.grip = {4, 1,       -2,      0,       0,       0,      1,      25,     -15,
                  2, right.x, right.y, right.z, right.w, left.x, left.y, left.z, left.w};
    return state;
}
void test_fixed_holds(const Rig& rig)
{
    for (unsigned weapon : {2u, 4u, 5u, 6u})
        for (Vec3 velocity :
             {rig.forward * 160, rig.forward * -160, rig.right * 100, rig.forward * 400})
        {
            CombatAnimator animator;
            auto state = calibrated_rifle();
            state.weapon = weapon;
            state.flags = length(velocity) > 300 ? Sprinting : 0;
            constexpr std::array<Bone, 6> bones{UpperArmL, LowerArmL, HandL,
                                                UpperArmR, LowerArmR, HandR};
            std::optional<std::array<BonePose, bones.size()>> held;
            Presentation last;
            for (int frame = 0; frame < 210; ++frame)
            {
                last = animator.evaluate(rig, velocity, state, frame / 30.0);
                if (frame < 60)
                    continue;
                const auto chest = inverse(last.pose[Spine3].q);
                std::array<BonePose, bones.size()> relative;
                for (std::size_t i = 0; i < bones.size(); ++i)
                {
                    const auto& bone = last.pose[bones[i]];
                    relative[i] = {rotate(chest, bone.p - last.pose[Spine3].p), chest * bone.q};
                }
                if (!held)
                    held = relative;
                for (std::size_t i = 0; i < bones.size(); ++i)
                {
                    require(length(relative[i].p - (*held)[i].p) < .001f,
                            "fixed firearm hold has no arm drift through the gait cycle");
                    for (Vec3 axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}})
                        require(length(rotate(relative[i].q, axis) - rotate((*held)[i].q, axis)) <
                                    .0001f,
                                "fixed firearm hold has no shoulder, forearm or wrist rotation");
                }
            }
            const auto carry = last;
            state.flags |= Aiming;
            state.pitch = 20;
            for (int frame = 210; frame < 240; ++frame)
                last = animator.evaluate(rig, velocity, state, frame / 30.0);
            require(length(last.pose[HandR].p - carry.pose[HandR].p) > .1f,
                    "fixed locomotion hold still responds to aiming");
            const auto aimed = last;
            state.flags |= Firing;
            ++state.shot;
            last = animator.evaluate(rig, velocity, state, 8);
            require(length(last.weapon.p - aimed.weapon.p) > .01f,
                    "fixed locomotion hold still responds to recoil");
        }
}
void test(const Pose& reference)
{
    Rig rig(reference);
    test_fixed_holds(rig);
    Animator idle;
    auto rest = idle.evaluate(rig, {}, 0);
    for (auto chain :
         {std::array{UpperArmL, LowerArmL, HandL}, std::array{UpperArmR, LowerArmR, HandR}})
        require(dot(rest[chain[2]].p - rest[chain[1]].p, rig.forward) > 0,
                "unarmed forearms point forward from the elbow");
    for (const auto x : {Vec3{1, 0, 0}, Vec3{0, -1, 0}, Vec3{-1, 0, 0}, Vec3{0, 0, 1}})
    {
        const auto y = unit(cross(std::abs(x.z) < 0.8f ? Vec3{0, 0, 1} : Vec3{0, 1, 0}, x));
        const auto q = frame_rotation(x, y);
        require(length(rotate(q, {1, 0, 0}) - x) < 0.0001f &&
                    length(rotate(q, {0, 1, 0}) - y) < 0.0001f,
                "anatomical frame quaternion preserves both axes");
    }
    CombatAnimator animator;
    auto state = calibrated_rifle();
    for (int frame = 0; frame < 1200; ++frame)
    {
        const double now = frame / 60.0;
        state.flags = 0;
        state.ability = 0;
        state.pitch = 0;
        Vec3 velocity{160, 0, 0};
        if (frame >= 180 && frame < 300)
        {
            state.flags = Crouched;
            velocity = {0, 90, 0};
        }
        if (frame >= 300 && frame < 420)
            state.pitch = static_cast<float>((frame - 300) / 119.0 * 140 - 70);
        if (frame >= 420 && frame < 480)
        {
            state.flags = Firing;
            if (frame % 6 == 0)
                ++state.shot;
        }
        if (frame >= 480 && frame < 540)
            state.flags = Reloading;
        if (frame >= 540 && frame < 960)
        {
            state.flags = Casting;
            state.ability = 1 + (frame - 540) / 60;
        }
        if (frame >= 960 && frame < 1020)
            state.flags = Airborne;
        if (frame >= 1020 && frame < 1080)
        {
            state.flags = Sprinting;
            velocity = {400, 0, 0};
        }
        if (frame >= 1080)
        {
            state.weapon = 9;
            state.flags = Melee;
            if (frame % 30 == 0)
                ++state.action;
        }
        const auto result = animator.evaluate(rig, velocity, state, now);
        for (const auto& bone : result.pose)
            require(valid_vector(bone.p), "combat poses remain finite");
        for (const auto chain :
             {std::array{ThighL, CalfL, FootL}, std::array{ThighR, CalfR, FootR},
              std::array{UpperArmL, LowerArmL, HandL}, std::array{UpperArmR, LowerArmR, HandR}})
            for (int i = 0; i < 2; ++i)
                require(std::abs(length(result.pose[chain[i + 1]].p - result.pose[chain[i]].p) -
                                 length(reference[chain[i + 1]].p - reference[chain[i]].p)) <
                            0.005f,
                        "combat limbs retain calibrated lengths");
        if (frame > 60 && frame < 420)
        {
            const auto weapon_from_wrist =
                rotate(inverse(result.weapon.q), result.weapon.p - result.pose[HandR].p);
            require(length(weapon_from_wrist - Vec3{4, 1, -2}) < 0.001f,
                    "weapon stays rigidly anchored to solved carrier wrist");
            const auto left_from_wrist =
                rotate(inverse(result.weapon.q), result.pose[HandL].p - result.pose[HandR].p);
            if (length(left_from_wrist - Vec3{25, -15, 2}) >= 0.02f)
                std::cerr << "grip frame=" << frame
                          << " error=" << length(left_from_wrist - Vec3{25, -15, 2}) << " cm\n";
            require(length(left_from_wrist - Vec3{25, -15, 2}) < 0.02f,
                    "support hand stays on measured weapon grip while walking and aiming");
        }
    }
    // Crouching bends the legs; it must not rotate planted soles into the floor.
    for (const auto velocity : {Vec3{}, Vec3{0, 60, 0}, Vec3{70, 0, 0}})
    {
        CombatAnimator crouching;
        for (int frame = 0; frame < 360; ++frame)
        {
            auto current = calibrated_rifle();
            if (frame >= 60 && frame < 240)
                current.flags = Crouched;
            const auto result = crouching.evaluate(rig, velocity, current, frame / 60.0);
            const float left = sole_height(rig, result.pose, FootL),
                        right = sole_height(rig, result.pose, FootR);
            if (left < -.01f || right < -.01f)
                std::cerr << "crouch frame=" << frame << " soles=" << left << ',' << right
                          << " cm\n";
            require(left >= -.01f && right >= -.01f,
                    "crouch and recovery retain heel/toe ground clearance");
            if (crouching.has_support())
                require(std::min(std::abs(left), std::abs(right)) < .01f,
                        "crouch retains a planted sole");
        }
    }
    // Horizontal movement must not keep a running shoulder cycle in flight.
    for (unsigned weapon : {0u, 1u})
    {
        CombatAnimator stationary, moving;
        float lifted = 0;
        for (int frame = 0; frame < 300; ++frame)
        {
            auto current = calibrated_rifle();
            current.weapon = weapon;
            if (frame < 180)
                current.flags = Airborne;
            const auto a_jump = stationary.evaluate(rig, {}, current, frame / 60.0);
            const auto b_jump = moving.evaluate(rig, {300, 0, 0}, current, frame / 60.0);
            if (frame >= 90 && frame < 180)
                for (auto shoulder : {UpperArmL, UpperArmR})
                    require(length((a_jump.pose[shoulder].p - a_jump.pose[Pelvis].p) -
                                   (b_jump.pose[shoulder].p - b_jump.pose[Pelvis].p)) < .1f,
                            "airborne shoulders follow jump instead of horizontal running cadence");
            if (frame < 90)
                lifted = std::max(lifted, std::max(a_jump.pose[FootL].p.z, a_jump.pose[FootR].p.z) -
                                              std::max(rig.rest[FootL].p.z, rig.rest[FootR].p.z));
            if (frame >= 240)
                for (auto foot : {FootL, FootR})
                    require(sole_height(rig, a_jump.pose, foot) >= -.01f,
                            "jump landing returns soles above ground");
        }
        require(lifted > 15, "authored jump raises a knee instead of a rigid airborne stance");
    }
    // Stabilizing an armed torso must attenuate running roll without changing
    // the authored pelvis and feet, which would cause sliding or lost contact.
    for (auto velocity : {Vec3{160, 0, 0}, Vec3{-160, 0, 0}, Vec3{0, 100, 0}, Vec3{400, 0, 0}})
    {
        Animator free_walk, steady_walk;
        std::optional<Quat> old_free, old_steady;
        std::optional<Vec3> old_free_anchor, old_steady_anchor;
        float free_steps = 0, steady_steps = 0;
        float free_anchor_steps = 0, steady_anchor_steps = 0;
        auto turn = [](Quat a, Quat b)
        {
            const float cosine = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
            return 2 * std::acos(std::clamp(cosine, 0.0f, 1.0f));
        };
        for (int frame = 0; frame < 360; ++frame)
        {
            const auto free = free_walk.evaluate(rig, velocity, frame / 60.0);
            const auto steady = steady_walk.evaluate(rig, velocity, frame / 60.0, nullptr, 0, .85f);
            for (auto bone : {Pelvis, ThighL, CalfL, FootL, BallL, ThighR, CalfR, FootR, BallR})
                require(length(free[bone].p - steady[bone].p) < .001f,
                        "armed upper-body stabilization preserves pelvis and foot trajectories");
            if (frame > 90 && old_free && old_steady)
            {
                free_steps += turn(*old_free, free[Spine3].q);
                steady_steps += turn(*old_steady, steady[Spine3].q);
            }
            old_free = free[Spine3].q;
            old_steady = steady[Spine3].q;
            const auto free_anchor = free[Spine1].p - free[Pelvis].p;
            const auto steady_anchor = steady[Spine1].p - steady[Pelvis].p;
            if (frame > 90 && old_free_anchor && old_steady_anchor)
            {
                free_anchor_steps += length(free_anchor - *old_free_anchor);
                steady_anchor_steps += length(steady_anchor - *old_steady_anchor);
            }
            old_free_anchor = free_anchor;
            old_steady_anchor = steady_anchor;
        }
        require(steady_steps < free_steps * .6f,
                "armed shoulders attenuate the unarmed walking roll");
        require(steady_anchor_steps < free_anchor_steps * .6f,
                "armed spine anchor counteracts pelvis swing without moving the feet");
    }
    CombatAnimator historical, plain;
    auto old = calibrated_rifle();
    old.shot = 100;
    const auto a = historical.evaluate(rig, {}, old, 0),
               b = plain.evaluate(rig, {}, calibrated_rifle(), 0);
    require(length(a.weapon.p - b.weapon.p) < 0.001f,
            "initial shot watermark does not replay recoil");
    auto firing = old;
    firing.flags = Firing;
    const auto c = historical.evaluate(rig, {}, firing, 1.0 / 30);
    require(length(c.weapon.p - b.weapon.p) > 0.01f,
            "continuous firing animates even without an ammo decrement");
    for (unsigned id = 1; id <= 12; ++id)
        require(weapon_assets(static_cast<std::uint8_t>(id)) != nullptr,
                "supported weapon ID has fixed cosmetic assets");
    require(!weapon_assets(13), "unknown weapon cannot select arbitrary assets");
}
int main(int argc, char** argv)
{
    try
    {
        auto reference = gait_fixture();
        if (argc > 1)
        {
            std::ifstream source(argv[1]);
            for (auto& bone : reference)
                if (!(source >> bone.p.x >> bone.p.y >> bone.p.z >> bone.q.x >> bone.q.y >>
                      bone.q.z >> bone.q.w))
                    throw std::runtime_error("invalid combat reference");
        }
        test(reference);
        std::cout << "Combat pose/rigid grip/action/weapon tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
