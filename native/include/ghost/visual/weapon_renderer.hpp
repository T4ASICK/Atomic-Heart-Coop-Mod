#pragma once
#include <cmath>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include "ghost/visual/visual_state.hpp"
#include "ghost/visual/visual_resources.hpp"
#include "ghost/visual/weapon_assets.hpp"
#include "ghost/engine/transform_validation.hpp"

namespace ghost
{
class WeaponRenderer
{
    using Call = ue::Reflection::Call;
    ue::Api& api;
    ue::Reflection& reflection;
    VisualResources& resources;

public:
    WeaponRenderer(ue::Api& a, ue::Reflection& r, VisualResources& cache)
        : api(a), reflection(r), resources(cache)
    {
    }
    void clear(Visual& visual)
    {
        for (const auto& part : visual.weapon_parts)
            if (auto* object = api.weak_get(&part.component))
            {
                Call destroy(reflection, L"/Script/Engine.ActorComponent:K2_DestroyComponent");
                destroy.set(L"Object", object).run(object);
            }
        visual.weapon_parts.clear();
    }
    void update(Visual& visual, const CombatState& state)
    {
        const auto& pose = visual.procedural.presentation;
        const unsigned wanted = pose && pose->weapon_visible && pose->weapon_id == state.weapon &&
                                        state.calibrated && state.weapon <= 12
                                    ? state.weapon
                                    : 0;
        if (wanted != visual.weapon_kind)
        {
            clear(visual);
            visual.weapon_kind = wanted;
            visual.weapon_error.clear();
            if (const auto* profile = weapon_assets(static_cast<std::uint8_t>(wanted)))
            {
                try
                {
                    ue::Object* base = nullptr;
                    for (const auto& piece : profile->meshes)
                    {
                        auto* mesh = resources.asset(piece.mesh);
                        if (api.object_property(mesh, L"Skeleton") !=
                            resources.asset(piece.skeleton))
                            throw std::runtime_error("weapon cosmetic skeleton mismatch");
                        Call add(reflection, L"/Script/Engine.Actor:AddComponentByClass");
                        add.set(L"Class", api.require(L"/Script/Engine.SkeletalMeshComponent"))
                            .set(L"bManualAttachment", false)
                            .set(L"RelativeTransform", ue::Transform{})
                            .set(L"bDeferredFinish", false)
                            .run(api.weak_get(&visual.actor));
                        auto* component = add.get<ue::Object*>();
                        if (!component)
                            throw std::runtime_error("weapon cosmetic creation failed");
                        visual.weapon_parts.push_back({api.weak(component), {}});
                        resources.mobility(component);
                        Call collision(reflection,
                                       L"/Script/Engine.PrimitiveComponent:SetCollisionEnabled");
                        collision.set(L"NewType", static_cast<unsigned char>(0)).run(component);
                        resources.boolean(
                            component,
                            L"/Script/Engine.PrimitiveComponent:SetGenerateOverlapEvents",
                            L"bInGenerateOverlapEvents", false);
                        Call set_mesh(reflection,
                                      L"/Script/Engine.SkinnedMeshComponent:SetSkeletalMesh");
                        set_mesh.set(L"NewMesh", mesh).set(L"bReinitPose", true).run(component);
                        if (wanted == 5)
                        {
                            // The first-person energy surface is unsuitable for
                            // a world-space peer: use a depth-tested opaque skin.
                            auto* opaque =
                                resources.asset(L"/Game/Mods/AHGhostCoopNavigation/"
                                                L"M_GhostWeaponOpaque.M_GhostWeaponOpaque");
                            Call slots(reflection,
                                       L"/Script/Engine.PrimitiveComponent:GetNumMaterials");
                            slots.run(component);
                            const int count = slots.get<int>();
                            if (count < 1 || count > 32)
                                throw std::runtime_error("unsupported Dominator material slots");
                            for (int i = 0; i < count; ++i)
                            {
                                Call material(reflection,
                                              L"/Script/Engine.PrimitiveComponent:SetMaterial");
                                material.set(L"ElementIndex", i)
                                    .set(L"Material", opaque)
                                    .run(component);
                            }
                        }
                        if (!piece.attachment_bone.empty())
                        {
                            if (!base || piece.follow_base_pose)
                                throw std::runtime_error("invalid independent weapon attachment");
                            ue::Name bone;
                            api.name_ctor(&bone, piece.attachment_bone.data(), 1, nullptr);
                            Call index(reflection,
                                       L"/Script/Engine.SkinnedMeshComponent:GetBoneIndex");
                            index.set(L"BoneName", bone).run(base);
                            if (index.get<int>() < 0)
                                throw std::runtime_error("weapon attachment bone missing");
                            Call socket(reflection,
                                        L"/Script/Engine.SceneComponent:GetSocketTransform");
                            socket.set(L"InSocketName", bone)
                                .set(L"TransformSpace", static_cast<unsigned char>(2))
                                .run(base);
                            const auto t = socket.get<ue::Transform>();
                            auto& attachment = visual.weapon_parts.back().attachment;
                            attachment.p = {t.translation[0], t.translation[1], t.translation[2]};
                            attachment.q = {t.rotation[0], t.rotation[1], t.rotation[2],
                                            t.rotation[3]};
                            const auto& q = attachment.q;
                            const float norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
                            if (!gait::valid_vector(attachment.p) || length(attachment.p) > 200 ||
                                !std::isfinite(norm) || std::abs(norm - 1) > .02f)
                                throw std::runtime_error("invalid weapon attachment frame");
                            if (!unit_xyz_scale(t.scale))
                                throw std::runtime_error("unsupported weapon attachment scale");
                            attachment.q = gait::normalized(attachment.q);
                        }
                        if (base && piece.follow_base_pose)
                        {
                            Call master(
                                reflection,
                                L"/Script/Engine.SkinnedMeshComponent:SetMasterPoseComponent");
                            master.set(L"NewMasterBoneComponent", base)
                                .set(L"bForceUpdate", true)
                                .run(component);
                        }
                        if (!base)
                            base = component;
                    }
                }
                catch (const std::exception& error)
                {
                    clear(visual);
                    visual.weapon_error = error.what();
                }
            }
        }
        if (!visual.procedural.presentation)
            return;
        const auto& frame = visual.procedural.presentation->weapon;
        for (const auto& part : visual.weapon_parts)
            if (auto* component = api.weak_get(&part.component))
            {
                const auto position = frame.p + gait::rotate(frame.q, part.attachment.p);
                const auto rotation = frame.q * part.attachment.q;
                ue::Transform transform;
                transform.translation[0] = position.x;
                transform.translation[1] = position.y;
                transform.translation[2] = position.z;
                transform.rotation[0] = rotation.x;
                transform.rotation[1] = rotation.y;
                transform.rotation[2] = rotation.z;
                transform.rotation[3] = rotation.w;
                Call move_part(reflection,
                               L"/Script/Engine.SceneComponent:K2_SetRelativeTransform");
                move_part.set(L"NewTransform", transform)
                    .set(L"bSweep", false)
                    .set(L"bTeleport", true)
                    .run(component);
            }
    }
};
} // namespace ghost
