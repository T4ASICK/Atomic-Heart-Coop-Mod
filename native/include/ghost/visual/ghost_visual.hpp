#pragma once
#include <algorithm>
#include <cmath>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include "ghost/visual/visual_state.hpp"
#include "ghost/visual/visual_resources.hpp"
#include "ghost/visual/weapon_renderer.hpp"

namespace ghost
{
class Renderer
{
    using Call = ue::Reflection::Call;
    std::optional<gait::WeaponBounds> stock_bounds;
    std::string stock_error;
    double stock_retry{};
    ue::Api& api;
    ue::Reflection& reflection;
    VisualResources resources{api, reflection};
    WeaponRenderer weapons{api, reflection, resources};
    Tags tags{api, reflection};

    ue::Object* compatible_asset(std::wstring_view path, const ModelAssets& profile)
    {
        auto* value = resources.asset(path);
        if (api.object_property(value, L"Skeleton") != resources.asset(profile.skeleton))
            throw std::runtime_error("ghost mesh/animation skeleton mismatch: " +
                                     ue::utf8(std::wstring(path)));
        return value;
    }
    void animation(Visual& visual, bool moving)
    {
        const auto& profile = model_assets(visual.model);
        const auto clip = moving ? profile.walk : profile.idle;
        if (clip.empty())
            throw std::runtime_error("movement requires the runner visual profile");
        auto* anim = profile.manual_pose ? compatible_asset(clip, profile) : resources.asset(clip);
        auto* component = api.weak_get(&visual.component);
        if (profile.manual_pose)
        {
            Call mode(reflection, L"/Script/Engine.SkeletalMeshComponent:SetAnimationMode");
            mode.set(L"InAnimationMode", static_cast<unsigned char>(1)).run(component);
            // SetAnimation creates a stopped single-node instance; unlike
            // PlayAnimation it does not start notifying cutscene events.
            Call set(reflection, L"/Script/Engine.SkeletalMeshComponent:SetAnimation");
            set.set(L"NewAnimToPlay", anim).run(component);
            Call instance(reflection, L"/Script/Engine.SkeletalMeshComponent:GetAnimInstance");
            instance.run(component);
            auto* single = instance.get<ue::Object*>();
            Call root_mode(reflection, L"/Script/Engine.AnimInstance:SetRootMotionMode");
            root_mode.set(L"Value", static_cast<unsigned char>(1)).run(single);
            Call duration(reflection, L"/Script/Engine.AnimSingleNodeInstance:GetLength");
            duration.run(single);
            visual.animation_length = duration.get<float>();
            if (!std::isfinite(visual.animation_length) || visual.animation_length <= 0 ||
                visual.animation_length > 3600)
                throw std::runtime_error("invalid story P3 clip length");
            visual.pose_clock.reset();
        }
        else
        {
            Call call(reflection, L"/Script/Engine.SkeletalMeshComponent:PlayAnimation");
            call.set(L"NewAnimToPlay", anim).set(L"bLooping", true).run(component);
        }
        visual.running = moving;
    }
    void assemble(Visual& visual, ue::Object* actor, ue::Object* body, const ModelAssets& profile)
    {
        for (auto path : profile.followers)
        {
            if (path.empty())
                continue;
            auto* mesh = compatible_asset(path, profile);
            Call add(reflection, L"/Script/Engine.Actor:AddComponentByClass");
            add.set(L"Class", api.require(L"/Script/Engine.SkeletalMeshComponent"))
                .set(L"bManualAttachment", false)
                .set(L"RelativeTransform", ue::Transform{})
                .set(L"bDeferredFinish", false)
                .run(actor);
            auto* part = add.get<ue::Object*>();
            if (!part)
                throw std::runtime_error("P3 mesh component creation failed");
            visual.followers.push_back(api.weak(part));
            resources.mobility(part);
            Call collision(reflection, L"/Script/Engine.PrimitiveComponent:SetCollisionEnabled");
            collision.set(L"NewType", static_cast<unsigned char>(0)).run(part);
            resources.boolean(part, L"/Script/Engine.PrimitiveComponent:SetGenerateOverlapEvents",
                              L"bInGenerateOverlapEvents", false);
            Call set_mesh(reflection, L"/Script/Engine.SkinnedMeshComponent:SetSkeletalMesh");
            set_mesh.set(L"NewMesh", mesh).set(L"bReinitPose", true).run(part);
            Call pose(reflection, L"/Script/Engine.SkinnedMeshComponent:SetMasterPoseComponent");
            pose.set(L"NewMasterBoneComponent", body).set(L"bForceUpdate", true).run(part);
        }
    }

public:
    CharacterModel model{CharacterModel::Runner};
    float feet_offset = -90.0f;
    float mesh_yaw_offset = 0.0f;
    float story_feet_offset = -90.0f;
    float story_mesh_yaw_offset = 0.0f;
    float procedural_feet_offset = -90.0f;
    float procedural_mesh_yaw_offset = 0.0f;
    float name_height = 210.0f;

    Renderer(ue::Api& a, ue::Reflection& r) : api(a), reflection(r) {}
    float feet_for(CharacterModel selected) const
    {
        if (selected == CharacterModel::ProceduralP3)
            return procedural_feet_offset;
        return selected == CharacterModel::StoryP3 ? story_feet_offset : feet_offset;
    }
    float yaw_for(CharacterModel selected) const
    {
        if (selected == CharacterModel::ProceduralP3)
            return procedural_mesh_yaw_offset;
        return selected == CharacterModel::StoryP3 ? story_mesh_yaw_offset : mesh_yaw_offset;
    }
    void move(ue::Object* actor, Vec3 position, float yaw)
    {
        Call call(reflection, L"/Script/Engine.Actor:K2_SetActorLocationAndRotation");
        call.set(L"NewLocation", position)
            .set(L"NewRotation", ue::Rotation{0, yaw, 0})
            .set(L"bSweep", false)
            .set(L"bTeleport", true)
            .run(actor);
        if (!call.get<bool>())
            throw std::runtime_error("actor movement failed (root must be movable)");
    }
    void remove(Visual& visual)
    {
        tags.remove(visual.tag);
        if (auto* object = api.weak_get(&visual.actor))
        {
            Call call(reflection, L"/Script/Engine.Actor:K2_DestroyActor");
            call.run(object);
        }
        visual = {};
    }
    void create(Visual& visual, ue::Object* world, const State& state,
                std::optional<CharacterModel> selected = {})
    {
        remove(visual);
        const auto actual = selected.value_or(
            visual_motion(model, std::hypot(state.velocity.x, state.velocity.y), false).model);
        Vec3 position = state.position + Vec3{0, 0, feet_for(actual)};
        ue::Rotation rotation{0, state.yaw + yaw_for(actual), 0};
        auto* actor = api.spawn(world, api.require(L"/Script/Engine.SkeletalMeshActor"), &position,
                                &rotation);
        if (!actor)
            throw std::runtime_error("SkeletalMeshActor spawn failed");
        visual.actor = api.weak(actor);
        visual.model = actual;
        try
        {
            resources.boolean(actor, L"/Script/Engine.Actor:SetActorEnableCollision",
                              L"bNewActorEnableCollision", false);
            auto* component = api.object_property(actor, L"SkeletalMeshComponent");
            if (!component)
                throw std::runtime_error("SkeletalMeshComponent missing");
            resources.mobility(component);
            if (actual == CharacterModel::ProceduralP3)
            {
                // Keep the actor's empty skeletal root as a transform anchor;
                // the poseable child owns our authored, in-place body pose.
                Call add(reflection, L"/Script/Engine.Actor:AddComponentByClass");
                add.set(L"Class", api.require(L"/Script/Engine.PoseableMeshComponent"))
                    .set(L"bManualAttachment", false)
                    .set(L"RelativeTransform", ue::Transform{})
                    .set(L"bDeferredFinish", false)
                    .run(actor);
                component = add.get<ue::Object*>();
                if (!component)
                    throw std::runtime_error("procedural body creation failed");
                resources.mobility(component);
                Call collision(reflection,
                               L"/Script/Engine.PrimitiveComponent:SetCollisionEnabled");
                collision.set(L"NewType", static_cast<unsigned char>(0)).run(component);
                resources.boolean(component,
                                  L"/Script/Engine.PrimitiveComponent:SetGenerateOverlapEvents",
                                  L"bInGenerateOverlapEvents", false);
            }
            visual.component = api.weak(component);
            const auto& profile = model_assets(actual);
            auto* p3 = actual != CharacterModel::Runner ? compatible_asset(profile.body, profile)
                                                        : resources.asset(profile.body);
            Call set_mesh(reflection, L"/Script/Engine.SkinnedMeshComponent:SetSkeletalMesh");
            set_mesh.set(L"NewMesh", p3).set(L"bReinitPose", true).run(component);
            if (actual == CharacterModel::ProceduralP3)
                visual.procedural.initialize(api, reflection, component);
            else
                animation(visual, false);
            assemble(visual, actor, component, profile);

            visual.ready = true;
        }
        catch (...)
        {
            remove(visual);
            throw;
        }
    }
    void update(Visual& visual, const State& state, Vec3 viewer, ue::Object* controller, double now)
    {
        auto* actor = api.weak_get(&visual.actor);
        if (!visual.ready || !actor)
            throw std::runtime_error("visual expired");
        const float speed = std::hypot(state.velocity.x, state.velocity.y);
        const auto motion = visual_motion(model, speed, visual.running);
        if (motion.model != visual.model)
        {
            auto* world = api.get_world(actor);
            if (!world)
                throw std::runtime_error("visual world expired during model switch");
            // The runner has a different skeleton: replace the owned visual
            // assembly instead of assigning its animation to the story body.
            create(visual, world, state, motion.model);
            actor = api.weak_get(&visual.actor);
        }
        const Vec3 position = state.position + Vec3{0, 0, feet_for(visual.model)};
        const auto& profile = model_assets(visual.model);
        const float mesh_yaw = state.yaw + yaw_for(visual.model);
        Vec3 mesh_origin = position;
        if (profile.manual_pose)
        {
            // Use the last evaluated pose, which is what the renderer currently
            // displays. Seeking below does not immediately evaluate bone transforms.
            const auto root =
                api.read_property<Vec3>(api.weak_get(&visual.component), L"RootBoneTranslation");
            mesh_origin = story_mesh_origin(position, mesh_yaw, root);
        }
        move(actor, mesh_origin, mesh_yaw);
        tags.world(visual.tag, controller, position + Vec3{0, 0, name_height}, state.name,
                   length(state.position - viewer));
        const bool moving = motion.moving;
        if (visual.model == CharacterModel::ProceduralP3)
        {
            const float angle = mesh_yaw * 0.017453292519943295f, c = std::cos(angle),
                        s = std::sin(angle);
            const Vec3 local_velocity{c * state.velocity.x + s * state.velocity.y,
                                      -s * state.velocity.x + c * state.velocity.y, 0};
            if (state.combat.weapon == 1 && state.combat.calibrated && !stock_bounds &&
                now >= stock_retry)
            {
                stock_retry = now + 2;
                try
                {
                    struct Bounds
                    {
                        Vec3 center, extent;
                        float radius;
                    };
                    static_assert(sizeof(Bounds) == 28);
                    const auto bounds = api.read_property<Bounds>(
                        resources.asset(weapon_meshes_ak47.back().mesh), L"ImportedBounds");
                    if (!gait::valid_vector(bounds.center) || !gait::valid_vector(bounds.extent) ||
                        bounds.extent.x <= 0 || bounds.extent.y <= 0 || bounds.extent.z <= 0 ||
                        length(bounds.center) + length(bounds.extent) > 200)
                        throw std::runtime_error("invalid rifle stock bounds");
                    stock_bounds = gait::WeaponBounds{bounds.center, bounds.extent};
                    stock_error.clear();
                }
                catch (const std::exception& error)
                {
                    stock_error = error.what();
                }
            }
            visual.procedural.update(reflection, api.weak_get(&visual.component), local_velocity,
                                     state.combat, now, stock_bounds);
            weapons.update(visual, state.combat);
            if (state.combat.weapon == 1 && !stock_error.empty())
                visual.weapon_error = "stock fallback: " + stock_error;
            visual.running = moving;
        }
        else if (moving != visual.running)
            animation(visual, moving);
        if (profile.manual_pose)
        {
            for (const auto& part : visual.followers)
                if (!api.weak_get(&part))
                    throw std::runtime_error("P3 mesh component expired");
            const float rate =
                moving ? std::clamp(speed / profile.reference_speed, 0.4f, 1.8f) : 1.0f;
            Call position_anim(reflection, L"/Script/Engine.SkeletalMeshComponent:SetPosition");
            position_anim
                .set(L"InPos", visual.pose_clock.advance(now, rate, visual.animation_length))
                .set(L"bFireNotifies", false)
                .run(api.weak_get(&visual.component));
        }
        else if (moving && visual.model == CharacterModel::Runner)
        {
            Call rate(reflection, L"/Script/Engine.SkeletalMeshComponent:SetPlayRate");
            rate.set(L"Rate", std::clamp(speed / 450.0f, 0.4f, 1.8f))
                .run(api.weak_get(&visual.component));
        }
    }
};
} // namespace ghost
