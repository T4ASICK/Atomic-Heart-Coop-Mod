#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include "ghost/core/state.hpp"
#include "ghost/engine/unreal_api.hpp"
#include "ghost/animation/procedural_gait.hpp"

namespace ghost
{
// Read-only queries, on the existing game thread. Never activate gameplay actions.
class CombatSampler
{
    ue::Api& api;
    ue::Reflection& reflection;
    ue::Weak previous_weapon, previous_pawn;
    CombatState cached;
    std::optional<int> ammo;
    double sampled{-1};
    bool active(ue::Object* pawn, unsigned char action)
    {
        ue::Reflection::Call call(reflection,
                                  L"/Script/AtomicHeart.AHBaseCharacter:IsAbilityActive");
        call.set(L"InAction", action).run(pawn);
        return call.get<bool>();
    }
    struct GripCache
    {
        bool valid{};
        std::array<float, 18> values{};
    };
    std::array<GripCache, 14> grips;
    double equipped_since{}, next_calibration{};
    gait::BonePose socket(ue::Object* mesh, const wchar_t* name, bool bone)
    {
        ue::Name id;
        api.name_ctor(&id, name, 1, nullptr);
        if (bone)
        {
            ue::Reflection::Call index(reflection,
                                       L"/Script/Engine.SkinnedMeshComponent:GetBoneIndex");
            index.set(L"BoneName", id).run(mesh);
            if (index.get<int>() < 0)
                throw std::runtime_error("FP grip bone unavailable");
        }
        ue::Reflection::Call read(reflection, L"/Script/Engine.SceneComponent:GetSocketTransform");
        read.set(L"InSocketName", id)
            .set(L"TransformSpace", static_cast<unsigned char>(0))
            .run(mesh);
        const auto t = read.get<ue::Transform>();
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(t.scale[axis]) || std::abs(t.scale[axis] - 1) > 0.01f)
                throw std::runtime_error("unsupported FP grip scale");
        return {{t.translation[0], t.translation[1], t.translation[2]},
                gait::normalized({t.rotation[0], t.rotation[1], t.rotation[2], t.rotation[3]})};
    }
    GripCache calibrate(ue::Object* pawn, ue::Object* weapon, float yaw, float pitch)
    {
        using namespace gait;
        auto* hands = api.object_property(pawn, L"FPHandsMesh");
        auto* gun = api.object_property(weapon, L"Mesh");
        if (!hands || !gun)
            throw std::runtime_error("FP grip meshes unavailable");
        const auto right = socket(hands, L"hand_r", true), left = socket(hands, L"hand_l", true);
        const auto root = socket(gun, L"None", false);
        const auto world_aim =
            axis_angle({0, 0, 1}, yaw * pi / 180) * axis_angle({0, 1, 0}, -pitch * pi / 180);
        const auto undo = inverse(world_aim);
        GripCache result;
        const auto offset = rotate(undo, root.p - right.p),
                   support = rotate(undo, left.p - right.p);
        const auto rotation = undo * root.q;
        if (!valid_vector(offset) || !valid_vector(support) || length(offset) > 200 ||
            length(support) > 150)
            throw std::runtime_error("FP grip calibration exceeds cosmetic bounds");
        const auto anatomical = [&](bool is_left)
        {
            const auto middle = socket(hands, is_left ? L"middle_01_l" : L"middle_01_r", true);
            const auto index = socket(hands, is_left ? L"index_01_l" : L"index_01_r", true);
            const auto pinky = socket(hands, is_left ? L"pinky_01_l" : L"pinky_01_r", true);
            return undo *
                   frame_rotation(middle.p - (is_left ? left.p : right.p), index.p - pinky.p);
        };
        const auto rq = anatomical(false), lq = anatomical(true);
        result.values = {offset.x,   offset.y,  offset.z,  rotation.x, rotation.y, rotation.z,
                         rotation.w, support.x, support.y, support.z,  rq.x,       rq.y,
                         rq.z,       rq.w,      lq.x,      lq.y,       lq.z,       lq.w};
        result.valid = true;
        return result;
    }

public:
    std::string calibration_error;
    CombatSampler(ue::Api& a, ue::Reflection& r) : api(a), reflection(r) {}
    CombatState sample(ue::Object* pawn, float yaw, float pitch, double now)
    {
        if (api.weak_get(&previous_pawn) != pawn)
        {
            previous_pawn = api.weak(pawn);
            previous_weapon = {};
            cached = {};
            ammo.reset();
            sampled = -1;
        }
        // Vehicle/menu pawns do not expose the character's FP hand mesh.
        // Do not invoke AHBaseCharacter getters on those unrelated objects.
        if (!api.object_property(pawn, L"FPHandsMesh"))
        {
            cached = {};
            return cached;
        }
        pitch = std::clamp(std::remainder(pitch, 360.0f), -89.0f, 89.0f);
        if (sampled >= 0 && now >= sampled && now - sampled < 0.05)
        {
            cached.pitch = pitch;
            return cached;
        }
        if (sampled < 0 || now < sampled)
            sampled = now;
        else
            sampled += std::floor((now - sampled) / 0.05) * 0.05;
        auto next = cached;
        next.pitch = pitch;
        next.flags = 0;
        next.ability = 0;
        ue::Reflection::Call equipped(reflection,
                                      L"/Script/AtomicHeart.AHBaseCharacter:GetCurrentWeapon");
        equipped.run(pawn);
        auto* weapon = equipped.get<ue::Object*>();
        const bool changed = api.weak_get(&previous_weapon) != weapon;
        if (changed)
        {
            equipped_since = now;
            next_calibration = now + 0.5;
        }
        next.weapon = weapon ? weapon_id(ue::utf8(api.get_name(weapon))) : 0;
        if (active(pawn, 7))
            next.flags |= Crouched;
        if (active(pawn, 4) || active(pawn, 25))
            next.flags |= Airborne;
        if (active(pawn, 3))
            next.flags |= Sprinting;
        if (active(pawn, 9))
            next.flags |= Aiming;
        if (active(pawn, 10))
            next.flags |= Reloading;
        if (active(pawn, 13))
            next.flags |= Melee;
        if (active(pawn, 12))
            next.flags |= melee_weapon(next.weapon) ? Melee : Firing;
        // Observed ECharacterAbilities enum, mapped to fixed cosmetic IDs.
        constexpr std::array<std::pair<unsigned, unsigned>, 12> abilities{{{38, 1},
                                                                           {55, 2},
                                                                           {50, 3},
                                                                           {49, 4},
                                                                           {66, 5},
                                                                           {71, 5},
                                                                           {63, 3},
                                                                           {35, 6},
                                                                           {20, 6},
                                                                           {74, 8},
                                                                           {75, 8},
                                                                           {5, 7}}};
        for (const auto [type, id] : abilities)
            if (active(pawn, static_cast<unsigned char>(type)))
            {
                next.ability = id;
                next.flags |= Casting;
                break;
            }
        if (weapon)
        {
            const auto state = api.read_property<unsigned char>(weapon, L"WeaponState");
            if (state == 2)
                next.flags |= melee_weapon(next.weapon) ? Melee : Firing;
            if (state == 3)
                next.flags |= Reloading;
            if (next.weapon >= 1 && next.weapon <= 12 && !grips[next.weapon].valid &&
                (state == 1 || state == 4) &&
                !(next.flags & (Reloading | Firing | Melee | Casting)) && now >= next_calibration &&
                now - equipped_since >= 0.5)
            {
                next_calibration = now + 2;
                try
                {
                    grips[next.weapon] = calibrate(pawn, weapon, yaw, pitch);
                    calibration_error.clear();
                }
                catch (const std::exception& error)
                {
                    calibration_error = error.what();
                }
            }
            if (firearm(next.weapon))
            {
                ue::Reflection::Call count(reflection,
                                           L"/Script/AtomicHeart.BaseWeapon:GetAmmoCount");
                count.set(L"bCountChambered", true).run(weapon);
                const int current = count.get<int>();
                if (api.weak_get(&previous_weapon) == weapon && ammo && current >= 0 &&
                    current < *ammo && *ammo - current < 1000 && !(next.flags & Reloading))
                    next.shot += static_cast<std::uint32_t>(*ammo - current);
                ammo = current;
            }
            else
                ammo.reset();
            previous_weapon = api.weak(weapon);
        }
        else
        {
            previous_weapon = {};
            ammo.reset();
        }
        next.calibrated = next.weapon < grips.size() && grips[next.weapon].valid;
        next.grip = next.calibrated ? grips[next.weapon].values : std::array<float, 18>{};
        if ((next.ability && next.ability != cached.ability) ||
            ((next.flags & ~cached.flags) & (Reloading | Melee | Airborne)))
            ++next.action;
        cached = next;
        return cached;
    }
};
} // namespace ghost
