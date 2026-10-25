#pragma once
#include <cmath>
#include <optional>
#include <string>
#include "ghost/engine/unreal_api.hpp"
#include "ghost/core/navigation_core.hpp"

namespace ghost
{
class Teleporter
{
    using Call = ue::Reflection::Call;
    ue::Api& api;
    ue::Reflection& reflection;
    TeleportCooldown cooldown;

public:
    Teleporter(ue::Api& a, ue::Reflection& r) : api(a), reflection(r) {}
    bool attempt(ue::Object* pawn, ue::Object* controller, const std::optional<State>& peer,
                 const State& local, double now, std::string& message)
    {
        message = "teleport: no fresh same-world peer";
        if (!peer || peer->world != local.world)
            return false;
        if (!cooldown.consume(now))
        {
            message = "teleport: wait 2 seconds";
            return false;
        }
        Call ignored(reflection, L"/Script/Engine.Controller:IsMoveInputIgnored");
        ignored.run(controller);
        Call paused(reflection, L"/Script/Engine.GameplayStatics:IsGamePaused");
        paused.set(L"WorldContextObject", pawn)
            .run(api.require(L"/Script/Engine.Default__GameplayStatics"));
        if (ignored.get<bool>() || paused.get<bool>())
        {
            message = "teleport: close menus and wait for player control";
            return false;
        }
        auto* capsule = api.object_property(pawn, L"CapsuleComponent");
        if (!capsule)
        {
            message = "teleport: player capsule unavailable";
            return false;
        }
        Call height(reflection, L"/Script/Engine.CapsuleComponent:GetScaledCapsuleHalfHeight");
        height.run(capsule);
        Call radius(reflection, L"/Script/Engine.CapsuleComponent:GetScaledCapsuleRadius");
        radius.run(capsule);
        const float half_height = height.get<float>(), r = radius.get<float>();
        if (!std::isfinite(r) || r < 10 || r > 150 ||
            !grounded_arrival({}, Vec3{0, 0, 1}, true, false, half_height))
        {
            message = "teleport: unsupported player capsule";
            return false;
        }
        auto* library = api.require(L"/Script/Engine.Default__KismetSystemLibrary");
        for (const auto& candidate : arrival_candidates(*peer, r))
        {
            Call trace(reflection, L"/Script/Engine.KismetSystemLibrary:LineTraceSingle");
            trace.set(L"WorldContextObject", pawn)
                .set(L"Start", candidate + Vec3{0, 0, 150})
                .set(L"End", candidate - Vec3{0, 0, 350})
                .set(L"TraceChannel", static_cast<unsigned char>(0))
                .set(L"bIgnoreSelf", true)
                .run(library);
            if (!trace.get<bool>())
                continue;
            Call hit(reflection, L"/Script/Engine.GameplayStatics:BreakHitResult");
            hit.copy(L"Hit", trace, L"OutHit");
            hit.run(api.require(L"/Script/Engine.Default__GameplayStatics"));
            auto destination = grounded_arrival(
                hit.get<Vec3>(L"ImpactPoint"), hit.get<Vec3>(L"ImpactNormal"),
                hit.get<bool>(L"bBlockingHit"), hit.get<bool>(L"bInitialOverlap"), half_height);
            if (!destination)
                continue;
            // Engine TeleportTo tests the pawn's collision volume and refuses an obstructed
            // arrival.
            Call teleport(reflection, L"/Script/Engine.Actor:K2_TeleportTo");
            teleport.set(L"DestLocation", *destination)
                .set(L"DestRotation", ue::Rotation{0, local.yaw, 0})
                .run(pawn);
            if (!teleport.get<bool>())
                continue;
            if (auto* movement = api.object_property(pawn, L"CharacterMovement"))
            {
                Call stop(reflection, L"/Script/Engine.MovementComponent:StopMovementImmediately");
                stop.run(movement);
            }
            message = "teleported to " + peer->name;
            return true;
        }
        message = "teleport: no loaded, walkable and unobstructed arrival near peer";
        return false;
    }
};
} // namespace ghost
