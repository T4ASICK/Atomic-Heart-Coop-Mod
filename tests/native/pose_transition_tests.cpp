#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <istream>
#include <optional>
#include <stdexcept>
#include "ghost/animation/combat_animation.hpp"
#include <string>
#include "fixtures/gait_fixture.hpp"
#include <fstream>
#include <iostream>

namespace
{
using namespace ghost;
using namespace ghost::gait;
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
float degrees(float radians)
{
    return radians * 180 / pi;
}
float angle(Vec3 a, Vec3 b)
{
    return degrees(std::acos(std::clamp(dot(unit(a), unit(b)), -1.0f, 1.0f)));
}
float rotation_step(Quat a, Quat b)
{
    const auto delta = a * inverse(b);
    return degrees(2 * std::acos(std::clamp(std::abs(delta.w), 0.0f, 1.0f)));
}
void read_pose(std::istream& source, BonePose& bone)
{
    if (!(source >> bone.p.x >> bone.p.y >> bone.p.z >> bone.q.x >> bone.q.y >> bone.q.z >>
          bone.q.w))
        throw std::runtime_error("invalid pose reference file");
}
std::array<BonePose, 15> synthetic_fingers(const Pose& body, int side)
{
    const auto wrist = side == 0 ? HandL : HandR, elbow = side == 0 ? LowerArmL : LowerArmR;
    const auto along = unit(body[wrist].p - body[elbow].p);
    const auto across = unit(cross({0, 0, 1}, along));
    std::array<BonePose, 15> result;
    for (int finger = 0; finger < 5; ++finger)
        for (int joint = 0; joint < 3; ++joint)
            result[finger * 3 + joint] = {body[wrist].p + along * (8.0f + joint * 3.0f) +
                                              across * (2.0f - finger * 2.0f),
                                          {}};
    return result;
}
std::array<Quat, 2> hand_orientations(const Presentation& result,
                                      const std::array<HandRig, 2>& hands)
{
    std::array<Quat, 2> q;
    for (int side = 0; side < 2; ++side)
    {
        require(result.orient_hand[side],
                "anatomical orientation remains continuous even when unarmed");
        q[side] = hands[side].orient_axes(result.finger_axis[side], result.thumb_axis[side]);
    }
    return q;
}
void check_lengths(const Rig& rig, const Pose& pose)
{
    for (const auto chain :
         {std::array{ThighL, CalfL, FootL}, std::array{ThighR, CalfR, FootR},
          std::array{UpperArmL, LowerArmL, HandL}, std::array{UpperArmR, LowerArmR, HandR}})
        for (int i = 0; i < 2; ++i)
            require(std::abs(length(pose[chain[i + 1]].p - pose[chain[i]].p) -
                             length(rig.rest[chain[i + 1]].p - rig.rest[chain[i]].p)) < 0.005f,
                    "transition retains calibrated limb lengths");
}
void check_contact(const Presentation& p, const CombatState& state)
{
    const auto root = normalized({state.grip[3], state.grip[4], state.grip[5], state.grip[6]});
    const auto frame = p.weapon.q * inverse(root);
    const auto root_offset = rotate(inverse(frame), p.weapon.p - p.pose[HandR].p);
    const auto support = rotate(inverse(frame), p.pose[HandL].p - p.pose[HandR].p);
    require(length(root_offset - Vec3{state.grip[0], state.grip[1], state.grip[2]}) < 0.002f,
            "pitched weapon remains attached to solved carrier wrist");
    require(length(support - support_grip(state)) < 0.02f,
            "pitched support hand retains measured rigid contact");
}
void test(const Pose& reference, const std::array<HandRig, 2>& hands, CombatState state,
          const std::optional<WeaponBounds>& stock_bounds = {})
{
    Rig rig(reference);
    const auto rotation = axis_angle(unit({1, 2, 3}), 0.9f);
    const Vec3 a{3, 0, 0}, b{2, 2, 0};
    const auto mapped = limb_rotations(a, b, rotate(rotation, a), rotate(rotation, b));
    for (int i = 0; i < 2; ++i)
    {
        const auto segment = i == 0 ? a : b;
        require(length(rotate(mapped[i], segment) - rotate(rotation, segment)) < 0.0001f,
                "limb frame maps segment direction");
        require(length(rotate(mapped[i], unit(cross(a, b))) - rotate(rotation, unit(cross(a, b)))) <
                    0.0001f,
                "limb frame maps full bend-plane orientation");
        require(std::abs(mapped[i].x * mapped[i].x + mapped[i].y * mapped[i].y +
                         mapped[i].z * mapped[i].z + mapped[i].w * mapped[i].w - 1) < 0.0001f,
                "limb frame rotation is normalized");
    }
    const auto settle = [&](CombatState target)
    {
        CombatAnimator animator;
        Presentation result;
        for (int frame = 0; frame < 150; ++frame)
            result = animator.evaluate(rig, {}, target, frame / 30.0, stock_bounds);
        return result;
    };
    if (state.weapon == 2 || state.weapon == 4)
    {
        CombatAnimator sweeping;
        std::optional<Pose> last;
        for (int frame = 0; frame < 720; ++frame)
        {
            auto current = state;
            current.flags = Aiming;
            current.pitch = 75 * std::sin(static_cast<float>(frame) * 2 * pi / 180);
            const auto held = sweeping.evaluate(rig, {}, current, frame / 30.0, stock_bounds);
            check_lengths(rig, held.pose);
            check_contact(held, current);
            for (int side = 0; side < 2; ++side)
            {
                const auto upper = side == 0 ? UpperArmL : UpperArmR,
                           elbow = side == 0 ? LowerArmL : LowerArmR;
                const auto wrist = side == 0 ? HandL : HandR;
                const auto segment = held.pose[elbow].p - held.pose[upper].p;
                const auto axis = unit(segment);
                const auto swing = from_to(rig.rest[elbow].p - rig.rest[upper].p, segment);
                const auto full = held.pose[upper].q * inverse(rig.rest[upper].q);
                const auto turn = normalized(full * inverse(swing));
                const float twist = std::remainder(
                    2 * std::atan2(dot(Vec3{turn.x, turn.y, turn.z}, axis), turn.w), 2 * pi);
                require(std::abs(twist) < .751f, "pistol extreme aim bounds humeral axial twist");
                require(angle(held.pose[wrist].p - held.pose[elbow].p, held.finger_axis[side]) <
                            45.1f,
                        "pistol extreme aim keeps wrist inside anatomical cone");
                if (last && frame > 30)
                    require(length(held.pose[elbow].p - (*last)[elbow].p) < 4,
                            "pistol up/down sweep does not flip elbow branch");
            }
            last = held.pose;
        }
    }
    float minimum_height = 10000, maximum_height = -10000, maximum_wrist_bend = 0;
    for (const bool aiming : {false, true})
        for (const float pitch : {-75.0f, 0.0f, 75.0f})
        {
            auto target = state;
            target.pitch = pitch;
            target.flags = aiming ? Aiming : 0;
            const auto result = settle(target);
            check_lengths(rig, result.pose);
            check_contact(result, target);
            const auto q = hand_orientations(result, hands);
            for (int side = 0; side < 2; ++side)
            {
                const auto wrist = side == 0 ? HandL : HandR,
                           elbow = side == 0 ? LowerArmL : LowerArmR;
                const auto fingers =
                    rotate(q[side] * inverse(reference[wrist].q), hands[side].fingers);
                const float bend = angle(result.pose[wrist].p - result.pose[elbow].p, fingers);
                maximum_wrist_bend = std::max(maximum_wrist_bend, bend);
                if (bend >= 65)
                    std::cerr << "pitch=" << pitch << " aiming=" << aiming << " side=" << side
                              << " bend=" << bend
                              << " forearm=" << unit(result.pose[wrist].p - result.pose[elbow].p).x
                              << "," << unit(result.pose[wrist].p - result.pose[elbow].p).y << ","
                              << unit(result.pose[wrist].p - result.pose[elbow].p).z
                              << " fingers=" << fingers.x << "," << fingers.y << "," << fingers.z
                              << '\n';
                require(bend < 65,
                        "view pitch is carried by the arms rather than extreme wrist bend");
            }
            if (!aiming)
            {
                minimum_height = std::min(minimum_height, result.pose[HandR].p.z);
                maximum_height = std::max(maximum_height, result.pose[HandR].p.z);
            }
        }
    // Long support grips limit vertical carrier travel once torso clearance and
    // both arm reaches are enforced. The carrier must still follow pitch.
    const float support_span = length(Vec3{state.grip[7], state.grip[8], state.grip[9]});
    if (heavy_weapon(state.weapon))
    {
        auto high = state, low = state;
        high.pitch = 75;
        low.pitch = -75;
        require(rotation_step(settle(high).weapon.q, settle(low).weapon.q) > 25 &&
                    maximum_height - minimum_height < 25,
                "heavy weapon changes aim within a low carry rather than raising both arms to the "
                "face");
    }
    else
        require(maximum_height - minimum_height >
                    std::clamp(25 - 0.65f * support_span, 4.0f, 25.0f),
                "carrier follows pitch within the two-arm clearance envelope");
    auto aimed = state;
    aimed.flags = Aiming;
    if (heavy_weapon(state.weapon))
        require(std::abs(settle(aimed).pose[HandR].p.z - settle(state).pose[HandR].p.z) < 8,
                "heavy aiming retains a supported low carry instead of a rifle shoulder raise");
    else if (state.weapon == 1 && stock_bounds)
    {
        for (float pitch : {-75.0f, 0.0f, 75.0f})
        {
            auto current = state;
            current.pitch = pitch;
            const auto held = settle(current);
            const BodyClearance body(rig, held.pose);
            const auto pocket =
                held.pose[UpperArmR].p + body.forward * 10 - body.right * 2 - Vec3{0, 0, 5};
            const auto stock =
                held.pose[HandR].p +
                rotate(held.weapon.q * inverse(normalized({state.grip[3], state.grip[4],
                                                           state.grip[5], state.grip[6]})),
                       rifle_butt_in_hand(*stock_bounds, state));
            require(length(stock - pocket) < 6,
                    "rifle stock stays at the shoulder pocket within arm reach limits");
        }
    }
    else
        require(settle(aimed).pose[HandR].p.z - settle(state).pose[HandR].p.z > 10,
                "aiming raises the weapon above low ready");

    CombatAnimator animator;
    std::optional<std::array<Quat, 2>> previous;
    std::optional<std::array<float, 2>> previous_grasp;
    float maximum_cast_step = 0;
    constexpr std::array<unsigned, 8> ability_cycle{1, 2, 3, 4, 5, 6, 8, 2};
    for (int frame = 0; frame < 660; ++frame)
    {
        auto current = state;
        if (frame < 30 || frame >= 630)
        {
            current.weapon = 0;
            current.calibrated = false;
        }
        if (frame >= 90 && frame < 570)
        {
            const auto phase = static_cast<std::size_t>((frame - 90) / 60);
            if ((frame - 90) % 60 < 30)
            {
                current.flags = Casting;
                current.ability = ability_cycle[phase];
            }
            current.action = static_cast<std::uint32_t>(phase + 1);
        }
        else if (frame >= 570)
            current.action = 8;
        const auto result = animator.evaluate(rig, {}, current, frame / 30.0, stock_bounds);
        check_lengths(rig, result.pose);
        const auto q = hand_orientations(result, hands);
        if (previous)
            for (int side = 0; side < 2; ++side)
            {
                const float step = rotation_step(q[side], (*previous)[side]);
                if (step >= 35)
                    std::cerr << "transition frame=" << frame << " side=" << side
                              << " step=" << step << '\n';
                require(step < 35,
                        "equip, cast and recovery hand orientation have no threshold snap");
                if (frame >= 90 && frame < 630)
                    maximum_cast_step = std::max(maximum_cast_step, step);
                require(std::abs(result.grasp[side] - (*previous_grasp)[side]) < 0.4f,
                        "finger grasp blends continuously from closed through relaxed to open");
            }
        previous = q;
        previous_grasp = result.grasp;
    }

    CombatAnimator switch_cast;
    std::optional<std::array<float, 2>> previous_switch_grasp;
    for (int frame = 0; frame < 300; ++frame)
    {
        auto current = state;
        current.flags = Casting;
        current.ability = frame < 90 ? 6 : 2;
        const auto result = switch_cast.evaluate(rig, {}, current, frame / 30.0, stock_bounds);
        if (previous_switch_grasp)
            require(std::abs(result.grasp[0] - (*previous_switch_grasp)[0]) < 0.4f,
                    "switching glove abilities eases finger opening too");
        previous_switch_grasp = result.grasp;
    }

    // The lift gesture owns only the free arm. Compare identical timelines so
    // gait phase, head tracking and the rigid gun carrier cannot mask a change.
    for (unsigned ability : {4u, 6u})
    {
        CombatAnimator neutral, lifting;
        for (int frame = 0; frame < 180; ++frame)
        {
            auto base = state, gesture = state;
            base.flags = 0;
            gesture.flags = frame >= 30 && frame < 120 ? Casting : 0;
            gesture.ability = ability;
            const auto untouched = neutral.evaluate(rig, {}, base, frame / 30.0, stock_bounds);
            const auto lifted = lifting.evaluate(rig, {}, gesture, frame / 30.0, stock_bounds);
            for (std::size_t bone = 0; bone < Count; ++bone)
            {
                if (bone == UpperArmL || bone == LowerArmL || bone == HandL)
                    continue;
                require(length(untouched.pose[bone].p - lifted.pose[bone].p) < .0001f &&
                            rotation_step(untouched.pose[bone].q, lifted.pose[bone].q) < .1f,
                        "lift gesture preserves the other arm, legs, trunk and head");
            }
            require(length(untouched.weapon.p - lifted.weapon.p) < .0001f &&
                        rotation_step(untouched.weapon.q, lifted.weapon.q) < .1f &&
                        length(untouched.finger_axis[1] - lifted.finger_axis[1]) < .0001f &&
                        untouched.grasp[1] == lifted.grasp[1],
                    "lift gesture preserves the gun and carrier hand");
            if (frame >= 75 && frame < 120)
            {
                const auto forearm = unit(lifted.pose[HandL].p - lifted.pose[LowerArmL].p);
                const auto palm = unit(cross(lifted.finger_axis[0], lifted.thumb_axis[0])) * -1;
                require(dot(palm, Vec3{0, 0, 1}) > .8f, "lift palm faces upward");
                require(angle(forearm, lifted.finger_axis[0]) < 45,
                        "lift wrist has no sharp flexion");
                require(dot(lifted.pose[HandL].p - lifted.pose[Spine3].p, rig.forward) > 35,
                        "lift hand reaches forward away from the face");
                require(lifted.grasp[0] > -.4f && lifted.grasp[0] < .1f,
                        "lift fingers remain relaxed");
            }
        }
    }
    if (state.weapon == 1 || state.weapon == 5)
    {
        CombatAnimator lowered;
        auto down = state;
        down.pitch = -62;
        down.flags = Aiming;
        for (int frame = 0; frame < 90; ++frame)
        {
            const auto result = lowered.evaluate(rig, {}, down, frame / 30.0, stock_bounds);
            for (auto chain : {std::array{UpperArmL, LowerArmL}, std::array{UpperArmR, LowerArmR}})
                require(result.pose[chain[1]].p.z <= result.pose[chain[0]].p.z,
                        "AK and Dominator downward aim keeps elbows below shoulders");
        }
    }
    CombatAnimator reload;
    std::optional<Vec3> previous_support;
    float maximum_reload_step = 0;
    for (int frame = 0; frame < 210; ++frame)
    {
        auto current = state;
        if (frame >= 90 && frame < 120)
            current.flags = Reloading;
        const auto result = reload.evaluate(rig, {}, current, frame / 30.0, stock_bounds);
        check_lengths(rig, result.pose);
        if (frame >= 90 && frame < 180 && previous_support)
        {
            const float step = length(result.pose[HandL].p - *previous_support);
            maximum_reload_step = std::max(maximum_reload_step, step);
            require(step < 8, "reload hand target eases both into and out of the gesture");
        }
        previous_support = result.pose[HandL].p;
    }
    CombatAnimator plain, jumping;
    for (int frame = 0; frame < 300; ++frame)
    {
        auto a_state = state, b_state = state;
        if (frame >= 60)
            a_state.flags = b_state.flags = Reloading;
        if (frame >= 75)
            b_state.action = 1;
        if (frame >= 75 && frame < 90)
            b_state.flags |= Airborne;
        const auto first = plain.evaluate(rig, {}, a_state, frame / 30.0, stock_bounds);
        const auto second = jumping.evaluate(rig, {}, b_state, frame / 30.0, stock_bounds);
        if (frame >= 180)
            require(length(first.pose[HandL].p - second.pose[HandL].p) < 0.001f,
                    "unrelated airborne action does not reset the reload phase");
    }
    std::cout << "Pose transitions passed; maximum settled wrist bend=" << maximum_wrist_bend
              << " degrees; maximum cast hand step=" << maximum_cast_step
              << " degrees; maximum reload hand step=" << maximum_reload_step << " cm at 30 Hz\n";
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        auto body = gait_fixture();
        if (argc > 1)
        {
            std::ifstream file(argv[1]);
            for (auto& bone : body)
                read_pose(file, bone);
        }
        std::array<std::array<BonePose, 15>, 2> fingers{synthetic_fingers(body, 0),
                                                        synthetic_fingers(body, 1)};
        if (argc > 2)
        {
            std::ifstream file(argv[2]);
            for (auto& hand : fingers)
                for (auto& bone : hand)
                    read_pose(file, bone);
        }
        const std::array<HandRig, 2> hands{HandRig(body[HandL], fingers[0], true),
                                           HandRig(body[HandR], fingers[1], false)};
        CombatState state;
        state.weapon = 4;
        state.calibrated = true;
        state.grip = {0, 0, 0, 0, 0, 0, 1, 0, -5, -6, 0, 0, 0, 1, 0, 0, 0, 1};
        if (argc > 3)
        {
            std::ifstream file(argv[3]);
            for (auto& value : state.grip)
                if (!(file >> value))
                    throw std::runtime_error("invalid grip fixture file");
        }
        if (argc > 4)
            state.weapon = static_cast<unsigned>(std::stoul(argv[4]));
        std::optional<WeaponBounds> stock_bounds;
        if (argc > 5)
        {
            WeaponBounds bounds;
            std::ifstream file(argv[5]);
            if (!(file >> bounds.center.x >> bounds.center.y >> bounds.center.z >>
                  bounds.extent.x >> bounds.extent.y >> bounds.extent.z))
                throw std::runtime_error("invalid stock bounds fixture");
            stock_bounds = bounds;
        }
        test(body, hands, state, stock_bounds);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
