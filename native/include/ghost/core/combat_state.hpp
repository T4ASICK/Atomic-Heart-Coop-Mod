#pragma once
#include <cstdint>
#include <array>
#include <string_view>

namespace ghost
{
enum CombatFlag : unsigned
{
    Crouched = 1,
    Airborne = 2,
    Sprinting = 4,
    Aiming = 8,
    Firing = 16,
    Reloading = 32,
    Melee = 64,
    Casting = 128
};
enum class WeaponId : unsigned
{
    None,
    AK47,
    PM,
    KS23,
    Electro,
    Dominator,
    Railgun,
    FatBoy,
    Pashtet,
    Shved,
    Snowball,
    Zvezdochka,
    Fox,
    Unknown
};
struct CombatState
{
    unsigned weapon{}, flags{}, ability{};
    float pitch{};
    std::uint32_t shot{}, action{};
    bool calibrated{};
    std::array<float, 18> grip{};
};
inline bool firearm(unsigned id)
{
    return id >= 1 && id <= 7;
}
inline bool melee_weapon(unsigned id)
{
    return id >= 8 && id <= 12;
}
inline unsigned weapon_id(std::string_view name)
{
    if (name.starts_with("BP_AK47_C"))
        return 1;
    if (name.starts_with("BP_PM_C"))
        return 2;
    if (name.starts_with("BP_ShotgunKS23_C") || name.starts_with("BP_KS23_C"))
        return 3;
    if (name.starts_with("BP_Electro_C"))
        return 4;
    if (name.starts_with("BP_Dominator_C"))
        return 5;
    if (name.starts_with("BP_Railgun_C"))
        return 6;
    if (name.starts_with("BP_Krepysh_C"))
        return 7;
    if (name.starts_with("BP_Pashtet_C"))
        return 8;
    if (name.starts_with("BP_Shved_C"))
        return 9;
    if (name.starts_with("BP_Snejok_C"))
        return 10;
    if (name.starts_with("BP_Zvezdochka_C"))
        return 11;
    if (name.starts_with("BP_Lisa_C"))
        return 12;
    return name.empty() ? 0 : 13;
}
} // namespace ghost
