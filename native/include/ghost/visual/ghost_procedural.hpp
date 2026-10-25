#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include "ghost/animation/combat_animation.hpp"
#include "ghost/animation/arm_twist.hpp"
#include "ghost/engine/unreal_api.hpp"

namespace ghost
{
struct ProceduralPose
{
    std::optional<gait::Rig> rig;
    gait::CombatAnimator animator;
    std::optional<gait::Presentation> presentation;
    std::array<gait::BonePose, gait::Count + 6 + 30> requested_pose{};
    std::uint64_t pose_sequence{};
    bool requested_pose_valid{};
    std::array<ue::Name, gait::Count> bones;
    std::array<ue::Transform, gait::Count> reference;
    gait::PoseCadence cadence;
    std::optional<gait::ArmTwist> twist;
    std::array<ue::Name, 6> twist_bones;
    std::array<ue::Transform, 6> twist_reference;
    struct Hand
    {
        std::optional<gait::HandRig> rig;
        std::array<ue::Name, 15> bones;
        std::array<ue::Transform, 15> reference;
    };
    std::array<Hand, 2> hands;

    void initialize(ue::Api& api, ue::Reflection& reflection, ue::Object* component)
    {
        gait::Pose rest;
        for (std::size_t i = 0; i < gait::Count; ++i)
        {
            api.name_ctor(&bones[i], gait::bone_names[i], 1, nullptr);
            ue::Reflection::Call index(reflection,
                                       L"/Script/Engine.SkinnedMeshComponent:GetBoneIndex");
            index.set(L"BoneName", bones[i]).run(component);
            if (index.get<int>() < 0)
                throw std::runtime_error("procedural P3 bone missing: " +
                                         ue::utf8(gait::bone_names[i]));
            ue::Reflection::Call read(
                reflection, L"/Script/Engine.PoseableMeshComponent:GetBoneTransformByName");
            read.set(L"BoneName", bones[i])
                .set(L"BoneSpace", static_cast<unsigned char>(1))
                .run(component);
            reference[i] = read.get<ue::Transform>();
            const auto& t = reference[i];
            for (int axis = 0; axis < 3; ++axis)
                if (!std::isfinite(t.scale[axis]) || std::abs(t.scale[axis] - 1) > 0.01f)
                    throw std::runtime_error("unsupported procedural reference scale");
            rest[i] = {{t.translation[0], t.translation[1], t.translation[2]},
                       {t.rotation[0], t.rotation[1], t.rotation[2], t.rotation[3]}};
        }
        rig.emplace(rest);
        std::array<gait::BonePose, 6> twist_rest;
        for (std::size_t i = 0; i < twist_rest.size(); ++i)
        {
            api.name_ctor(&twist_bones[i], gait::twist_names[i], 1, nullptr);
            ue::Reflection::Call index(reflection,
                                       L"/Script/Engine.SkinnedMeshComponent:GetBoneIndex");
            index.set(L"BoneName", twist_bones[i]).run(component);
            if (index.get<int>() < 0)
                throw std::runtime_error("procedural twist bone missing");
            ue::Reflection::Call read(
                reflection, L"/Script/Engine.PoseableMeshComponent:GetBoneTransformByName");
            read.set(L"BoneName", twist_bones[i])
                .set(L"BoneSpace", static_cast<unsigned char>(1))
                .run(component);
            const auto t = read.get<ue::Transform>();
            twist_reference[i] = t;
            twist_rest[i] = {{t.translation[0], t.translation[1], t.translation[2]},
                             {t.rotation[0], t.rotation[1], t.rotation[2], t.rotation[3]}};
        }
        twist.emplace(*rig, twist_rest);
        constexpr std::array<const wchar_t*, 5> fingers{L"index", L"middle", L"ring", L"pinky",
                                                        L"thumb"};
        for (int side = 0; side < 2; ++side)
        {
            auto& hand = hands[side];
            std::array<gait::BonePose, 15> finger_rest;
            for (int finger = 0; finger < 5; ++finger)
                for (int joint = 0; joint < 3; ++joint)
                {
                    const int i = finger * 3 + joint;
                    const auto name = std::wstring(fingers[finger]) + L"_0" +
                                      std::to_wstring(joint + 1) + (side == 0 ? L"_l" : L"_r");
                    api.name_ctor(&hand.bones[i], name.c_str(), 1, nullptr);
                    ue::Reflection::Call index(reflection,
                                               L"/Script/Engine.SkinnedMeshComponent:GetBoneIndex");
                    index.set(L"BoneName", hand.bones[i]).run(component);
                    if (index.get<int>() < 0)
                        throw std::runtime_error("procedural finger bone missing: " +
                                                 ue::utf8(name));
                    ue::Reflection::Call read(
                        reflection, L"/Script/Engine.PoseableMeshComponent:GetBoneTransformByName");
                    read.set(L"BoneName", hand.bones[i])
                        .set(L"BoneSpace", static_cast<unsigned char>(1))
                        .run(component);
                    const auto t = read.get<ue::Transform>();
                    hand.reference[i] = t;
                    finger_rest[i] = {{t.translation[0], t.translation[1], t.translation[2]},
                                      gait::normalized({t.rotation[0], t.rotation[1], t.rotation[2],
                                                        t.rotation[3]})};
                }
            hand.rig.emplace(rig->rest[side == 0 ? gait::HandL : gait::HandR], finger_rest,
                             side == 0);
        }
    }
    void update(ue::Reflection& reflection, ue::Object* component, Vec3 velocity,
                const CombatState& combat, double now,
                const std::optional<gait::WeaponBounds>& stock_bounds = {})
    {
        if (!rig)
            throw std::runtime_error("procedural rig is not initialized");
        // Each reflected setter rebuilds a temporary pose over the full skeleton.
        // Start at 30 Hz until actual in-game cost has been measured.
        if (!cadence.due(now))
            return;
        requested_pose_valid = false;
        presentation = animator.evaluate(*rig, velocity, combat, now, stock_bounds, &*hands[0].rig);
        auto& pose = presentation->pose;
        for (int side = 0; side < 2; ++side)
        {
            const auto elbow = side == 0 ? gait::LowerArmL : gait::LowerArmR;
            const auto wrist = side == 0 ? gait::HandL : gait::HandR;
            pose[wrist].q =
                presentation->orient_hand[side]
                    ? hands[side].rig->orient_axes(presentation->finger_axis[side],
                                                   presentation->thumb_axis[side])
                    : hands[side].rig->orientation(pose[elbow].p, pose[wrist].p, rig->forward);
        }
        const auto twist_pose = twist->apply(pose);
        std::copy(pose.begin(), pose.end(), requested_pose.begin());
        std::copy(twist_pose.begin(), twist_pose.end(), requested_pose.begin() + gait::Count);
        for (std::size_t i = 0; i < gait::Count; ++i)
        {
            auto transform = reference[i];
            const auto& p = pose[i];
            transform.translation[0] = p.p.x;
            transform.translation[1] = p.p.y;
            transform.translation[2] = p.p.z;
            transform.rotation[0] = p.q.x;
            transform.rotation[1] = p.q.y;
            transform.rotation[2] = p.q.z;
            transform.rotation[3] = p.q.w;
            ue::Reflection::Call set(
                reflection, L"/Script/Engine.PoseableMeshComponent:SetBoneTransformByName");
            set.set(L"BoneName", bones[i])
                .set(L"InTransform", transform)
                .set(L"BoneSpace", static_cast<unsigned char>(1))
                .run(component);
        }
        for (std::size_t i = 0; i < twist_pose.size(); ++i)
        {
            auto transform = twist_reference[i];
            const auto& p = twist_pose[i];
            transform.translation[0] = p.p.x;
            transform.translation[1] = p.p.y;
            transform.translation[2] = p.p.z;
            transform.rotation[0] = p.q.x;
            transform.rotation[1] = p.q.y;
            transform.rotation[2] = p.q.z;
            transform.rotation[3] = p.q.w;
            ue::Reflection::Call set(
                reflection, L"/Script/Engine.PoseableMeshComponent:SetBoneTransformByName");
            set.set(L"BoneName", twist_bones[i])
                .set(L"InTransform", transform)
                .set(L"BoneSpace", static_cast<unsigned char>(1))
                .run(component);
        }
        for (int side = 0; side < 2; ++side)
        {
            const auto& hand = hands[side];
            const auto& wrist = pose[side == 0 ? gait::HandL : gait::HandR];
            const auto fingers = hand.rig->evaluate(wrist, presentation->grasp[side]);
            std::copy(fingers.begin(), fingers.end(),
                      requested_pose.begin() + gait::Count + 6 + side * 15);
            for (std::size_t i = 0; i < fingers.size(); ++i)
            {
                auto transform = hand.reference[i];
                const auto& p = fingers[i];
                transform.translation[0] = p.p.x;
                transform.translation[1] = p.p.y;
                transform.translation[2] = p.p.z;
                transform.rotation[0] = p.q.x;
                transform.rotation[1] = p.q.y;
                transform.rotation[2] = p.q.z;
                transform.rotation[3] = p.q.w;
                ue::Reflection::Call set(
                    reflection, L"/Script/Engine.PoseableMeshComponent:SetBoneTransformByName");
                set.set(L"BoneName", hand.bones[i])
                    .set(L"InTransform", transform)
                    .set(L"BoneSpace", static_cast<unsigned char>(1))
                    .run(component);
            }
        }
        requested_pose_valid = true;
        ++pose_sequence;
    }
};
} // namespace ghost
