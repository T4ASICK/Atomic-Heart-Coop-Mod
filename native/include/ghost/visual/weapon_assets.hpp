#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace ghost
{
// Fixed cosmetic-only assets observed in the installed normal-weapon Blueprint
// imports. Evidence: local/combat-weapon-imports.json and
// local/combat-weapon-mesh-skeletons.json. No gameplay classes or supplied paths.
// Import presence does not establish upgraded appearance or grip transforms.
struct WeaponMesh
{
    std::wstring_view mesh, skeleton;
    // False for the first/base piece and independently attached magazines.
    // A false follower remains in reference pose; its attachment is separate.
    bool follow_base_pose;
    // Independent pieces use this bone's component-space frame on the base.
    // Never copy master pose across different reference frames/bone sets.
    std::wstring_view attachment_bone{};
};
struct WeaponAssets
{
    std::uint8_t id;
    std::string_view name;
    std::span<const WeaponMesh> meshes; // First element is always the base.
};

inline constexpr std::array<WeaponMesh, 5> weapon_meshes_ak47{{
    {L"/Game/Development/Weapons/AK-47/SK_AK47_Base.SK_AK47_Base",
     L"/Game/Development/Weapons/AK-47/SKEL_AK47_02.SKEL_AK47_02", false},
    {L"/Game/Development/Weapons/AK-47/SK_AK47_Barrel_1.SK_AK47_Barrel_1",
     L"/Game/Development/Weapons/AK-47/SKEL_AK47_02.SKEL_AK47_02", true},
    {L"/Game/Development/Weapons/AK-47/SK_AK47_Magazine_1.SK_AK47_Magazine_1",
     L"/Game/Development/Weapons/AK-47/SKEL_AK47_02.SKEL_AK47_02", false, L"mag_jnt"},
    {L"/Game/Development/Weapons/AK-47/SK_AK47_Receiver_1.SK_AK47_Receiver_1",
     L"/Game/Development/Weapons/AK-47/SKEL_AK47_02.SKEL_AK47_02", true},
    {L"/Game/Development/Weapons/AK-47/SK_AK47_Stock_1.SK_AK47_Stock_1",
     L"/Game/Development/Weapons/AK-47/SKEL_AK47_02.SKEL_AK47_02", true},
}};

inline constexpr std::array<WeaponMesh, 3> weapon_meshes_pm{{
    {L"/Game/Development/Weapons/PM/SK_PM_Base0.SK_PM_Base0",
     L"/Game/Development/Weapons/PM/SKEL_PM.SKEL_PM", false},
    {L"/Game/Development/Weapons/PM/SK_PM_BaseReceiver.SK_PM_BaseReceiver",
     L"/Game/Development/Weapons/PM/SKEL_PM.SKEL_PM", true},
    {L"/Game/Development/Weapons/PM/SK_PM_Mag.SK_PM_Mag",
     L"/Game/Development/Weapons/PM/SKEL_PM_Mag.SKEL_PM_Mag", false, L"mag_jnt"},
}};

inline constexpr std::array<WeaponMesh, 5> weapon_meshes_ks23{{
    {L"/Game/Development/Weapons/Shotgun/SK_ShotgunKS23_Base.SK_ShotgunKS23_Base",
     L"/Game/Development/Weapons/Shotgun/SKEL_ShotgunKS23.SKEL_ShotgunKS23", false},
    {L"/Game/Development/Weapons/Shotgun/SK_ShotgunKS23_Barrel_v1.SK_ShotgunKS23_Barrel_v1",
     L"/Game/Development/Weapons/Shotgun/SKEL_ShotgunKS23.SKEL_ShotgunKS23", true},
    {L"/Game/Development/Weapons/Shotgun/SK_ShotgunKS23_Forestock.SK_ShotgunKS23_Forestock",
     L"/Game/Development/Weapons/Shotgun/SKEL_ShotgunKS23.SKEL_ShotgunKS23", true},
    {L"/Game/Development/Weapons/Shotgun/SK_ShotgunKS23_Handle_01.SK_ShotgunKS23_Handle_01",
     L"/Game/Development/Weapons/Shotgun/SKEL_ShotgunKS23.SKEL_ShotgunKS23", true},
    {L"/Game/Development/Weapons/Shotgun/SK_ShotgunKS23_Reciever_01.SK_ShotgunKS23_Reciever_01",
     L"/Game/Development/Weapons/Shotgun/SKEL_ShotgunKS23.SKEL_ShotgunKS23", true},
}};

inline constexpr std::array<WeaponMesh, 3> weapon_meshes_electro{{
    {L"/Game/Development/Weapons/Electro/SK_Electro.SK_Electro",
     L"/Game/Development/Weapons/Electro/SKEL_Electro.SKEL_Electro", false},
    {L"/Game/Development/Weapons/Electro/SK_Electro_Barrel01.SK_Electro_Barrel01",
     L"/Game/Development/Weapons/Electro/SKEL_Electro.SKEL_Electro", true},
    {L"/Game/Development/Weapons/Electro/SK_Electro_BarrelEnd03.SK_Electro_BarrelEnd03",
     L"/Game/Development/Weapons/Electro/SKEL_Electro.SKEL_Electro", true},
}};

inline constexpr std::array<WeaponMesh, 1> weapon_meshes_dominator{{
    {L"/Game/Development/Weapons/Dominator/SK_Dominator_Base.SK_Dominator_Base",
     L"/Game/Development/Weapons/Dominator/SKEL_Dominator.SKEL_Dominator", false},
}};

inline constexpr std::array<WeaponMesh, 3> weapon_meshes_railgun{{
    {L"/Game/Development/Weapons/Railgun/SK_Railgun_Base.SK_Railgun_Base",
     L"/Game/Development/Weapons/Railgun/SKEL_Railgun.SKEL_Railgun", false},
    {L"/Game/Development/Weapons/Railgun/SK_Railgun_Barrel_Short.SK_Railgun_Barrel_Short",
     L"/Game/Development/Weapons/Railgun/SKEL_Railgun.SKEL_Railgun", true},
    {L"/Game/Development/Weapons/Railgun/SK_Railgun_Reserve1.SK_Railgun_Reserve1",
     L"/Game/Development/Weapons/Railgun/SKEL_Railgun.SKEL_Railgun", true},
}};

inline constexpr std::array<WeaponMesh, 3> weapon_meshes_fatboy{{
    {L"/Game/Development/Weapons/Krepysh/SK_Krepysh_Base.SK_Krepysh_Base",
     L"/Game/Development/Weapons/Krepysh/SKEL_Krepysh.SKEL_Krepysh", false},
    {L"/Game/Development/Weapons/Krepysh/SK_Krepysh_Barrel_01.SK_Krepysh_Barrel_01",
     L"/Game/Development/Weapons/Krepysh/SKEL_Krepysh.SKEL_Krepysh", true},
    {L"/Game/Development/Weapons/Krepysh/SK_Krepysh_GripA.SK_Krepysh_GripA",
     L"/Game/Development/Weapons/Krepysh/SKEL_Krepysh.SKEL_Krepysh", true},
}};

inline constexpr std::array<WeaponMesh, 2> weapon_meshes_pashtet{{
    {L"/Game/Development/Weapons/Pashtet/SK_Pashtet.SK_Pashtet",
     L"/Game/Development/Weapons/Pashtet/SKEL_Pashtet.SKEL_Pashtet", false},
    {L"/Game/Development/Weapons/Pashtet/SK_Pashtet_BladeBase.SK_Pashtet_BladeBase",
     L"/Game/Development/Weapons/Pashtet/SKEL_Pashtet.SKEL_Pashtet", true},
}};

inline constexpr std::array<WeaponMesh, 1> weapon_meshes_shved{{
    {L"/Game/Development/Weapons/Shved/SK_Shved_Base.SK_Shved_Base",
     L"/Game/Development/Weapons/Shved/SK_Shved_Base_Skeleton.SK_Shved_Base_Skeleton", false},
}};

inline constexpr std::array<WeaponMesh, 2> weapon_meshes_snowball{{
    {L"/Game/Development/Weapons/Snejok/SK_Snejok_Base.SK_Snejok_Base",
     L"/Game/Development/Weapons/Snejok/SKEL_Snejok.SKEL_Snejok", false},
    {L"/Game/Development/Weapons/Snejok/SK_Snejok_Head01.SK_Snejok_Head01",
     L"/Game/Development/Weapons/Snejok/SKEL_Snejok.SKEL_Snejok", true},
}};

inline constexpr std::array<WeaponMesh, 1> weapon_meshes_zvezdochka{{
    {L"/Game/Development/Weapons/Zvezdochka/SK_ZvezdochkaBase.SK_ZvezdochkaBase",
     L"/Game/Development/Weapons/Zvezdochka/SKEL_Zvezdochka.SKEL_Zvezdochka", false},
}};

inline constexpr std::array<WeaponMesh, 2> weapon_meshes_fox{{
    {L"/Game/Development/Weapons/Lisa/SK_Lisa_HandleBase.SK_Lisa_HandleBase",
     L"/Game/Development/Weapons/Lisa/SKEL_Lisa.SKEL_Lisa", false},
    {L"/Game/Development/Weapons/Lisa/SK_Lisa_Blade01.SK_Lisa_Blade01",
     L"/Game/Development/Weapons/Lisa/SKEL_Lisa.SKEL_Lisa", true},
}};

inline constexpr std::array<WeaponAssets, 12> cosmetic_weapons{{
    {1, "AK47", weapon_meshes_ak47},
    {2, "PM", weapon_meshes_pm},
    {3, "KS23", weapon_meshes_ks23},
    {4, "Electro", weapon_meshes_electro},
    {5, "Dominator", weapon_meshes_dominator},
    {6, "Railgun", weapon_meshes_railgun},
    {7, "FatBoy", weapon_meshes_fatboy},
    {8, "Pashtet", weapon_meshes_pashtet},
    {9, "Shved", weapon_meshes_shved},
    {10, "Snowball", weapon_meshes_snowball},
    {11, "Zvezdochka", weapon_meshes_zvezdochka},
    {12, "Fox", weapon_meshes_fox},
}};

inline constexpr const WeaponAssets* weapon_assets(std::uint8_t id)
{
    return id >= 1 && id <= cosmetic_weapons.size() ? &cosmetic_weapons[id - 1] : nullptr;
}

static_assert(weapon_meshes_ak47.size() + weapon_meshes_pm.size() + weapon_meshes_ks23.size() +
                  weapon_meshes_electro.size() + weapon_meshes_dominator.size() +
                  weapon_meshes_railgun.size() + weapon_meshes_fatboy.size() +
                  weapon_meshes_pashtet.size() + weapon_meshes_shved.size() +
                  weapon_meshes_snowball.size() + weapon_meshes_zvezdochka.size() +
                  weapon_meshes_fox.size() ==
              31);
} // namespace ghost
