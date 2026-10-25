#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include "ghost/core/combat_state.hpp"
#include "ghost/animation/hand_rig.hpp"
#include "ghost/animation/procedural_gait.hpp"
#include "ghost/animation/body_clearance.hpp"
#include "ghost/animation/weapon_hold.hpp"
#include "ghost/animation/pose_constraints.hpp"

namespace ghost::gait
{
struct Presentation
{
    Pose pose;
    BonePose weapon;
    bool weapon_visible{};
    unsigned weapon_id{};
    unsigned grip_iterations{};
    std::array<Vec3, 2> finger_axis{}, thumb_axis{};
    std::array<bool, 2> orient_hand{};
    std::array<float, 2> grasp{};
};

class CombatAnimator
{
    Animator walk;
    std::optional<double> previous;
    std::optional<std::uint32_t> shot;
    std::optional<std::uint32_t> action;
    unsigned previous_flags{};
    float equipped{}, crouch{}, air{}, cast{}, recoil{}, reload_time{}, melee_time{10};
    float view_pitch{}, cast_time{}, aiming{}, reload_blend{}, cast_grasp{-1};
    float flight_time{}, landing_time{10};
    bool was_airborne{};
    unsigned cast_type{}, previous_ability{};
    std::optional<Quat> cast_frame;
    std::array<std::optional<Quat>, 2> hand_frames;
    double elapsed{};
    std::array<std::optional<Vec3>, 2> previous_elbows;
    std::optional<BonePose> previous_carrier_chest;
    std::optional<CombatState> last_equipment;

public:
    bool has_support() const
    {
        return walk.has_support() && air < .1f;
    }
    Presentation evaluate(const Rig& rig, Vec3 velocity, const CombatState& state, double now,
                          const std::optional<WeaponBounds>& stock_bounds = {},
                          const HandRig* support_hand = nullptr)
    {
        const bool fresh = !previous;
        const float dt = previous ? static_cast<float>(std::clamp(now - *previous, 0.0, 0.1)) : 0;
        previous = now;
        elapsed += dt;
        const float alpha = 1 - std::exp(-dt / 0.12f);
        if (fresh)
            view_pitch = std::clamp(state.pitch, -75.0f, 75.0f);
        else
            view_pitch += (std::clamp(state.pitch, -75.0f, 75.0f) - view_pitch) * alpha;
        auto approach = [&](float& value, bool target)
        { value += ((target ? 1.0f : 0.0f) - value) * alpha; };
        const bool has_equipment = firearm(state.weapon) || melee_weapon(state.weapon);
        if (has_equipment)
            last_equipment = state;
        const auto& equipment = last_equipment ? *last_equipment : state;
        const bool ranged = firearm(equipment.weapon), melee = melee_weapon(equipment.weapon);
        const bool heavy = heavy_weapon(equipment.weapon), rifle = equipment.weapon == 1;
        const bool long_barrel = long_gun(equipment.weapon);
        const bool pistol = equipment.weapon == 2 || equipment.weapon == 4;
        if (fresh)
            equipped = has_equipment ? 1.0f : 0.0f;
        else
            equipped += ((has_equipment ? 1.0f : 0.0f) - equipped) * (1 - std::exp(-dt / 0.24f));
        approach(crouch, (state.flags & Crouched) != 0);
        const bool airborne = (state.flags & Airborne) != 0;
        if (airborne && !was_airborne)
            flight_time = 0;
        if (!airborne && was_airborne)
            landing_time = 0;
        if (airborne)
            flight_time += dt;
        landing_time += dt;
        was_airborne = airborne;
        approach(air, airborne);
        approach(aiming, (state.flags & Aiming) != 0);
        reload_blend += (((state.flags & Reloading) && ranged ? 1.0f : 0.0f) - reload_blend) *
                        (1 - std::exp(-dt / 0.28f));
        const bool casting = (state.flags & Casting) && state.ability != 7;
        cast += ((casting ? 1.0f : 0.0f) - cast) * (1 - std::exp(-dt / 0.18f));
        if (casting)
            cast_type = state.ability;
        cast_grasp += ((cast_type == 4 || cast_type == 6 ? -0.25f : -1.0f) - cast_grasp) *
                      (1 - std::exp(-dt / 0.18f));
        recoil *= std::exp(-dt / 0.075f);
        if (shot && *shot != state.shot && ranged)
            recoil = 1;
        if ((state.flags & Firing) && ranged)
            recoil = std::max(recoil, 0.12f + 0.08f * std::sin(static_cast<float>(elapsed) * 36));
        shot = state.shot;
        const bool event = action && *action != state.action;
        const bool changed_cast = casting && state.ability != previous_ability;
        if (casting && (!(previous_flags & Casting) || changed_cast))
            cast_time = 0;
        cast_time += dt;
        const bool unrelated_event =
            changed_cast || ((state.flags & ~previous_flags) & (Airborne | Casting)) != 0;
        if ((state.flags & Reloading) &&
            (!(previous_flags & Reloading) ||
             (event && !unrelated_event && !((state.flags & ~previous_flags) & Melee))))
            reload_time = 0;
        if ((state.flags & Melee) &&
            (!(previous_flags & Melee) ||
             (event && !unrelated_event && !((state.flags & ~previous_flags) & Reloading))))
            melee_time = 0;
        action = state.action;
        reload_time += dt;
        melee_time += dt;
        previous_flags = state.flags;
        previous_ability = state.ability;

        Presentation out{};
        // Override the locomotion source before retargeting, so an airborne
        // runner does not keep swinging its shoulders. No looping takeoff/land.
        auto jump = blend_motion(
            sample_motion_once(authored::jump_start,
                               .15f + .85f * std::clamp(flight_time / .30f, 0.0f, 1.0f)),
            sample_motion(authored::jump_loop, flight_time / authored::jump_loop.duration),
            std::clamp((flight_time - .20f) / .18f, 0.0f, 1.0f));
        float jump_weight = air;
        if (!airborne && landing_time < .38f)
        {
            const float progress = std::clamp(landing_time / .38f, 0.0f, 1.0f);
            jump = blend_motion(jump, sample_motion_once(authored::jump_land, .5f + .5f * progress),
                                1 - air);
            jump_weight = std::max(air, (1 - progress) * .75f);
        }
        const bool fixed_hold = fixed_firearm_hold(equipment.weapon);
        const float upper_stability =
            ranged ? equipped * (1 - air) * (fixed_hold ? 1.0f : .97f) : 0;
        out.pose =
            walk.evaluate(rig, velocity, now, &jump, jump_weight, upper_stability, fixed_hold);
        auto& pose = out.pose;
        const Vec3 up{0, 0, 1};
        const auto posture = Vec3{0, 0, -25 * crouch};
        // Move the trunk, then resolve both legs to the existing contact targets.
        for (std::size_t i = Pelvis; i < Count; ++i)
            if (i < ThighL)
                pose[i].p = pose[i].p + posture;
        const float lean =
            ((state.flags & Sprinting) ? 0.07f : 0.0f) + (state.ability == 7 ? 0.18f : 0.0f);
        const float stance_yaw =
            heavy ? (equipment.weapon == 5 || equipment.weapon == 6 ? .30f : .65f) * equipped
                  : (ranged && equipment.weapon != 2 && equipment.weapon != 4 ? 0.35f * equipped
                                                                              : 0.0f);
        const auto leaning = axis_angle(up, stance_yaw) * axis_angle(rig.right, lean);
        for (std::size_t i = Spine1; i < ThighL; ++i)
        {
            pose[i].p = pose[Pelvis].p + rotate(leaning, pose[i].p - pose[Pelvis].p);
            pose[i].q = leaning * pose[i].q;
        }
        // Let the head follow the sight line without moving the calibrated shoulders.
        const auto look = axis_angle(up, -stance_yaw * .8f) *
                          axis_angle(rig.right, -view_pitch * pi / 180 * 0.6f);
        pose[Head].p = pose[Neck].p + rotate(look, pose[Head].p - pose[Neck].p);
        pose[Neck].q = look * pose[Neck].q;
        pose[Head].q = look * pose[Head].q;
        // Protract the clavicles before reaching forward. Rotating the actual
        // collar-to-shoulder offset preserves its length and avoids stretching
        // a T-pose shoulder across the chest to reach first-person grip spacing.
        const float support_length =
            equipment.calibrated
                ? length(Vec3{equipment.grip[7], equipment.grip[8], equipment.grip[9]})
                : 25;
        const float collar = (heavy                   ? .24f
                              : equipment.weapon == 3 ? .42f
                              : rifle                 ? .28f
                              : pistol                ? .32f
                              : ranged ? 0.55f + 0.45f * std::clamp(support_length / 34, 0.0f, 1.0f)
                                       : 0.25f) *
                             equipped;
        const auto chest = pose[Spine3].q * inverse(rig.rest[Spine3].q);
        for (int side = 0; side < 2; ++side)
        {
            const auto clavicle = side == 0 ? ClavicleL : ClavicleR,
                       upper = side == 0 ? UpperArmL : UpperArmR;
            const auto protract =
                axis_angle(rotate(chest, up), (side == 0 ? 1.0f : -1.0f) * collar);
            const auto collar_position =
                pose[Spine3].p + rotate(chest, rig.rest[clavicle].p - rig.rest[Spine3].p);
            const auto shoulder =
                collar_position +
                rotate(protract * chest, rig.rest[upper].p - rig.rest[clavicle].p);
            pose[clavicle].p = pose[clavicle].p * (1 - equipped) + collar_position * equipped;
            pose[clavicle].q =
                blend_rotation(pose[clavicle].q, protract * chest * rig.rest[clavicle].q, equipped);
            pose[upper].p = pose[upper].p * (1 - equipped) + shoulder * equipped;
        }
        const BodyClearance body(rig, pose);
        const std::array<ArmReach, 2> reaches{
            ArmReach(length(rig.rest[LowerArmL].p - rig.rest[UpperArmL].p),
                     length(rig.rest[HandL].p - rig.rest[LowerArmL].p)),
            ArmReach(length(rig.rest[LowerArmR].p - rig.rest[UpperArmR].p),
                     length(rig.rest[HandR].p - rig.rest[LowerArmR].p))};
        auto solve = [&](Bone a, Bone b, Bone c, Vec3 start, Vec3 end, Vec3 pole)
        {
            const auto limb = solve_limb(start, end, pole, length(rig.rest[b].p - rig.rest[a].p),
                                         length(rig.rest[c].p - rig.rest[b].p));
            const auto rotation =
                limb_rotations(rig.rest[b].p - rig.rest[a].p, rig.rest[c].p - rig.rest[b].p,
                               limb.joint - start, limb.end - limb.joint);
            pose[a] = {start, rotation[0] * rig.rest[a].q};
            pose[b] = {limb.joint, rotation[1] * rig.rest[b].q};
            pose[c] = {limb.end, rotation[1] * rig.rest[c].q};
        };
        for (int side = 0; side < 2; ++side)
        {
            const auto thigh = side == 0 ? ThighL : ThighR, calf = side == 0 ? CalfL : CalfR;
            const auto foot = side == 0 ? FootL : FootR, ball = side == 0 ? BallL : BallR;
            const auto start = pose[thigh].p + posture;
            const auto target = pose[foot].p;
            const auto planted_ankle = pose[foot].q;
            solve(thigh, calf, foot, start, target, rig.forward);
            // Knee IK changes the calf frame, not the planted foot's frame.
            // Retain gait heel/toe contacts while bending into crouch.
            pose[foot].q = planted_ankle;
            const auto ankle = pose[foot].q * inverse(rig.rest[foot].q);
            pose[ball] = {pose[foot].p + rotate(ankle, rig.rest[ball].p - rig.rest[foot].p),
                          ankle * rig.rest[ball].q};
        }

        const float limit = heavy                               ? 18.0f
                            : pistol || (rifle && stock_bounds) ? 42.0f
                            : melee                             ? 25.0f
                                                                : 62.0f;
        // The railgun's rear housing reaches the chest before its barrel can
        // follow a steep downward camera angle in this supported carry.
        const float down_limit = equipment.weapon == 6 ? 12.0f : limit;
        const float pitch = (equipment.weapon == 3 ? shotgun_pitch(view_pitch, aiming)
                                                   : std::clamp(view_pitch, -down_limit, limit)) *
                            pi / 180;
        const float sprint = (state.flags & Sprinting) ? 1.0f : 0.0f;
        const auto aim = axis_angle(rig.right, -pitch + sprint * 0.18f);
        const auto forward = rotate(aim, rig.forward), vertical = rotate(aim, up);
        // The gun and BOTH grip targets share this frame; locomotion cannot swing a wrist
        // independently. Pitch the complete carrier arm about its shoulder, rather than bending the
        // wrist through the whole view angle at a fixed chest-height target.
        const float held_forward =
            (equipment.weapon == 2 || equipment.weapon == 4 ? 40.0f : 24.0f) + 4 * aiming;
        const float held_height =
            (equipment.weapon == 2 || equipment.weapon == 4 ? -10.0f : -18.0f) + 16 * aiming +
            (28 + 8 * aiming) * std::sin(pitch) - 8 * sprint;
        const float side_offset =
            ranged && equipment.weapon != 2 && equipment.weapon != 4 ? 5.0f : -12.0f;
        auto origin = pose[UpperArmR].p + body.forward * held_forward + body.right * side_offset +
                      up * held_height;
        origin = origin - forward * (recoil * 2);
        auto weapon_rotation = axis_angle(rig.right, -recoil * 0.035f) * aim;
        if (heavy)
        {
            const float carry_side = equipment.weapon == 6   ? 41.0f
                                     : equipment.weapon == 5 ? 38.0f
                                                             : 28.0f;
            origin = pose[Spine3].p + rig.forward * 34 + rig.right * carry_side -
                     up * (20 - 4 * aiming) + up * (12 * std::sin(pitch));
        }
        if (rifle && stock_bounds && equipment.calibrated)
        {
            const auto pocket = pose[UpperArmR].p + body.forward * 10 - body.right * 2 - up * 5;
            origin = pocket - rotate(weapon_rotation, rifle_butt_in_hand(*stock_bounds, equipment));
        }
        const auto fit_grips = [&](Vec3 candidate, Quat carrier)
        {
            const auto support = rotate(carrier, support_grip(equipment));
            // Fit one rigid grip frame to both arm reaches before solving either arm.
            for (int iteration = 0; iteration < 48; ++iteration)
            {
                const auto previous_candidate = candidate;
                candidate = body.wrist(candidate);
                candidate = body.wrist(candidate + support) - support;
                for (int side = 0; side < 2; ++side)
                {
                    const auto upper = side == 0 ? UpperArmL : UpperArmR;
                    const auto center = pose[upper].p - (side == 0 ? support : Vec3{});
                    auto carrier_reach = reaches[side];
                    // Reserve support reach for the palm correction after the
                    // wrist cone, rather than resting exactly on its boundary.
                    if (side == 0)
                        carrier_reach.maximum -= .5f * body.scale;
                    candidate = carrier_reach.project(center, candidate, body.forward);
                }
                ++out.grip_iterations;
                if (length(candidate - previous_candidate) < .0001f &&
                    reaches[0].error(pose[UpperArmL].p, candidate + support) < .001f &&
                    reaches[1].error(pose[UpperArmR].p, candidate) < .001f &&
                    length(body.wrist(candidate) - candidate) < .001f &&
                    length(body.wrist(candidate + support) - candidate - support) < .001f)
                    break;
            }
            return candidate;
        };
        if (ranged && equipment.calibrated)
            origin = fit_grips(origin, weapon_rotation);
        float melee_strike = 0;
        if (melee)
        {
            const float cycle = (state.flags & Melee) ? std::fmod(melee_time, 0.8f) : melee_time;
            melee_strike = cycle < 0.65f ? std::sin(pi * std::clamp(cycle / 0.65f, 0.0f, 1.0f)) : 0;
            origin = origin + rig.forward * (melee_strike * 20) - up * (melee_strike * 12);
            weapon_rotation = axis_angle(rig.right, melee_strike * 1.1f) * aim;
        }
        if (melee)
            // The attack already reaches forward. Ease out carry space before
            // the arm reaches its limit, keeping the shaft clear at rest.
            origin = body.wrist(origin) +
                     body.forward * (6 * body.scale * equipped * (1 - melee_strike));
        out.weapon = {origin, weapon_rotation};
        std::array<Vec3, 2> targets{origin + forward * (equipment.weapon == 2 ? 4.0f : 28.0f) -
                                        rig.right * 7 + vertical * 3,
                                    origin};
        if (equipment.calibrated)
            targets[0] = origin + rotate(weapon_rotation, support_grip(equipment));
        if (melee)
            targets[0] = pose[HandL].p - rig.forward * (melee_strike * 5) + up * (melee_strike * 8);
        Vec3 reload_target{};
        if (reload_blend > 0)
        {
            const float cycle = std::fmod(reload_time, 1.4f) / 1.4f;
            const float reach = std::sin(pi * cycle);
            reload_target = body.wrist(origin + rig.forward * (2 + reach * 6) -
                                       up * (4 + reach * 9) - rig.right * (8 + reach * 4));
            targets[0] = targets[0] * (1 - reload_blend) + reload_target * reload_blend;
        }
        const Vec3 cast_forward = rotate(axis_angle(rig.right, -pitch), rig.forward);
        auto cast_target = pose[Spine3].p + cast_forward * 47 - rig.right * 19 + up * 3;
        // Lift/control gesture: reach away from the face, with a gently rising
        // forearm. It changes only the free casting arm, never the gun carrier.
        if (cast_type == 4 || cast_type == 6)
            cast_target = pose[Spine3].p + rig.forward * 43 - rig.right * 24 + up * 9;
        if (cast_type == 5)
            cast_target = pose[Spine3].p + cast_forward * 29 - rig.right * 17 + up * 7;
        if (cast_type == 1)
            cast_target =
                cast_target +
                cast_forward * (4 * std::sin(pi * std::clamp(cast_time / 0.35f, 0.0f, 1.0f)));
        if (cast_type == 3)
        {
            const float progress = std::clamp(cast_time / 0.55f, 0.0f, 1.0f);
            cast_target = pose[Spine3].p + cast_forward * (20 + 27 * progress) - rig.right * 19 +
                          up * (4 + 18 * std::sin(pi * progress));
        }
        const float cast_pulse = 0.8f * std::sin(static_cast<float>(elapsed) * 8);
        cast_target = body.wrist(cast_target + up * cast_pulse);
        auto cast_fingers = vertical, cast_thumb = rig.right;
        if (cast_type == 4 || cast_type == 6)
        {
            cast_fingers = unit(rig.forward + up * .3f);
            cast_thumb = rig.right * -1;
        }
        const auto requested_cast_frame = frame_rotation(cast_fingers, cast_thumb);
        cast_frame = cast_frame ? blend_rotation(*cast_frame, requested_cast_frame, alpha)
                                : requested_cast_frame;
        // Pistol wrap, lateral railgun grip and launcher foregrip have different
        // palm orientations. Preserve their physical contact after wrist limits.
        const auto support_center = support_hand && (equipment.weapon == 2 ||
                                                     equipment.weapon == 6 || equipment.weapon == 7)
                                        ? std::optional<Vec3>(support_hand->grip_center())
                                        : std::nullopt;
        const auto requested_weapon_rotation = weapon_rotation;
        for (int order = 0; order < 2; ++order)
        {
            const int side = 1 - order; // Solve the carrier wrist before its supporting hand.
            const bool left = side == 0;
            const auto upper = left ? UpperArmL : UpperArmR, lower = left ? LowerArmL : LowerArmR,
                       hand = left ? HandL : HandR;
            const auto start = pose[upper].p;
            const auto unarmed_wrist = pose[hand].p;
            auto previous_elbow = previous_elbows[side];
            // Both held elbows must follow the torso translation before the
            // continuity solver runs. Leaving the supporting elbow behind in
            // component space turns the pelvis bob into a rotating forearm.
            if (ranged && (!left || fixed_hold) && previous_elbow && previous_carrier_chest)
                previous_elbow =
                    pose[Spine3].p + rotate(pose[Spine3].q * inverse(previous_carrier_chest->q),
                                            *previous_elbow - previous_carrier_chest->p);
            const auto previous_hand_frame = hand_frames[side];
            const auto support_frame =
                equipment.calibrated
                    ? weapon_rotation * normalized({equipment.grip[14], equipment.grip[15],
                                                    equipment.grip[16], equipment.grip[17]})
                    : weapon_rotation;
            const auto support_contact =
                targets[0] + rotate(support_frame, support_center.value_or(Vec3{}));
            // Refit after wrist limits rotate the carrier. Every refinement uses
            // the same history, so it does not advance animation time repeatedly.
            for (int refinement = 0; refinement < 8; ++refinement)
            {
                if (!left)
                    weapon_rotation = requested_weapon_rotation;
                previous_elbows[side] = previous_elbow;
                hand_frames[side] = previous_hand_frame;
                auto target = unarmed_wrist * (1 - equipped) + targets[side] * equipped;
                if (heavy)
                {
                    const float transition = 4 * equipped * (1 - equipped);
                    target = target + rig.forward * (12 * transition) +
                             rig.right * ((left ? -8.0f : 8.0f) * transition);
                }
                if (left)
                    target = target * (1 - cast) + cast_target * cast;
                target = body.wrist(target);
                target = reaches[side].project(start, target, body.forward);
                const auto neutral_pole =
                    rig.forward * -1.2f + rig.right * (left ? -0.6f : 0.6f) + up * -0.35f;
                auto pole = neutral_pole * (1 - equipped) + rotate(aim, neutral_pole) * equipped;
                if (left)
                    pole = pole * (1 - cast) + neutral_pole * cast;
                auto anatomical = frame_rotation(unit(target - start), rig.right);
                if (equipment.calibrated && !(left && melee))
                {
                    const int i = left ? 14 : 10;
                    anatomical = weapon_rotation *
                                 normalized({equipment.grip[i], equipment.grip[i + 1],
                                             equipment.grip[i + 2], equipment.grip[i + 3]});
                }
                if (left)
                    anatomical = blend_rotation(anatomical, *cast_frame, cast);
                const auto limb =
                    body.arm(start, target, length(rig.rest[lower].p - rig.rest[upper].p),
                             length(rig.rest[hand].p - rig.rest[lower].p), left,
                             rotate(anatomical, {1, 0, 0}), pole, previous_elbows[side], dt,
                             (long_barrel || heavy || pistol) && (!left || cast < .05f),
                             !left && ranged ? 1.5f
                             : pistol        ? 3.0f
                                             : 8.0f);
                previous_elbows[side] = limb.joint;
                const auto arm_rotation = limb_rotations(rig.rest[lower].p - rig.rest[upper].p,
                                                         rig.rest[hand].p - rig.rest[lower].p,
                                                         limb.joint - start, limb.end - limb.joint);
                // The elbow position sets the bend plane; upper-arm skin roll must
                // remain bounded for light guns, melee and free-hand gestures too.
                const auto upper_rotation = comfortable_upper_rotation(
                    arm_rotation[0], rig.rest[lower].p - rig.rest[upper].p, limb.joint - start);
                pose[upper] = {start, upper_rotation * rig.rest[upper].q};
                pose[lower] = {limb.joint, arm_rotation[1] * rig.rest[lower].q};
                pose[hand] = {limb.end, arm_rotation[1] * rig.rest[hand].q};
                if (!left && !equipment.calibrated)
                {
                    targets[0] = targets[0] + pose[HandR].p - origin;
                    out.weapon.p = pose[HandR].p;
                }
                // Anatomical frames use the same two axes as HandRig::orient_axes.
                // Keep them continuous through equip, release and glove recovery.
                const auto along = unit(pose[hand].p - pose[lower].p);
                auto neutral_thumb = rig.forward;
                if (length(neutral_thumb - along * dot(neutral_thumb, along)) < 1e-4f)
                    neutral_thumb = rig.right;
                const auto neutral_frame = frame_rotation(along, neutral_thumb);
                auto held_frame = neutral_frame;
                if (ranged)
                {
                    held_frame = frame_rotation(
                        left && equipment.weapon != 2 ? rig.right : vertical * -1, forward);
                }
                if (equipment.calibrated && !(left && melee))
                {
                    const int index = left ? 14 : 10;
                    const auto q =
                        weapon_rotation *
                        normalized({equipment.grip[index], equipment.grip[index + 1],
                                    equipment.grip[index + 2], equipment.grip[index + 3]});
                    held_frame = q;
                }
                auto hand_frame = blend_rotation(neutral_frame, held_frame, equipped);
                const float carrier_grasp = left && melee                   ? .15f
                                            : left && equipment.weapon == 5 ? equipped * .65f
                                                                            : equipped;
                out.grasp[side] = carrier_grasp;
                if (left)
                {
                    hand_frame = blend_rotation(hand_frame, *cast_frame, cast);
                    out.grasp[side] =
                        carrier_grasp * (1 - reload_blend * 0.65f) * (1 - cast) + cast_grasp * cast;
                }
                // A changing forearm direction can turn the neutral palm basis
                // quickly during equip. Ease the complete anatomical frame too,
                // rather than switching either of its axes independently.
                hand_frames[side] =
                    hand_frames[side]
                        ? blend_rotation(*hand_frames[side], hand_frame, 1 - std::exp(-dt / 0.15f))
                        : hand_frame;
                hand_frame = *hand_frames[side];
                // Keep the cone active during every gesture/equip/reload transition.
                // A continuous limit avoids a palm snap when cast crosses a threshold.
                const float held = ranged ? equipped * (left ? 1 - cast : 1.0f) : 0;
                const float held_limit = heavy || long_barrel || pistol ? 45.0f : 60.0f;
                const float maximum_bend = (60 * (1 - held) + held_limit * held) * pi / 180;
                hand_frame = wrist_cone(hand_frame, along, rig.right, maximum_bend);
                hand_frames[side] = hand_frame;
                out.orient_hand[side] = true;
                out.finger_axis[side] = rotate(hand_frame, {1, 0, 0});
                out.thumb_axis[side] = rotate(hand_frame, {0, 1, 0});
                if (!left && equipment.calibrated)
                {
                    // Bone limits may rotate the final palm away from the requested
                    // aim frame. Keep the weapon attached to that actual palm, then
                    // solve the support against its resulting rigid grip frame.
                    const auto calibrated_hand =
                        normalized({equipment.grip[10], equipment.grip[11], equipment.grip[12],
                                    equipment.grip[13]});
                    weapon_rotation = hand_frame * inverse(calibrated_hand);
                    out.weapon.p = pose[HandR].p +
                                   rotate(weapon_rotation, {equipment.grip[0], equipment.grip[1],
                                                            equipment.grip[2]});
                    out.weapon.q =
                        weapon_rotation * normalized({equipment.grip[3], equipment.grip[4],
                                                      equipment.grip[5], equipment.grip[6]});
                    if (!melee)
                    {
                        if (equipped > .99f && reload_blend < .01f && cast < .01f && refinement < 7)
                        {
                            const auto fitted = fit_grips(pose[HandR].p, weapon_rotation);
                            if (length(fitted - pose[HandR].p) > .001f)
                            {
                                targets[1] = fitted;
                                continue;
                            }
                        }
                        const auto support =
                            pose[HandR].p + rotate(weapon_rotation, support_grip(equipment));
                        targets[0] = support * (1 - reload_blend) + reload_target * reload_blend;
                    }
                }
                if (left && support_center && equipped > .999f && reload_blend < .001f &&
                    cast < .001f && refinement < 7)
                {
                    const auto fitted = support_contact - rotate(hand_frame, *support_center);
                    if (length(fitted - pose[HandL].p) > .001f)
                    {
                        targets[0] = fitted;
                        continue;
                    }
                }
                break;
            }
        }
        previous_carrier_chest = pose[Spine3];
        out.weapon_id = state.weapon;
        out.weapon_visible = equipped > 0.95f && state.calibrated && state.weapon != 0;
        return out;
    }
};
} // namespace ghost::gait
