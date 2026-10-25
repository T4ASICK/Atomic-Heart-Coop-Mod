#pragma once
#include <string>
#include <vector>
#include "ghost/visual/ghost_model.hpp"
#include "ghost/visual/ghost_procedural.hpp"
#include "ghost/visual/ghost_tag.hpp"

namespace ghost
{
struct Visual
{
    ue::Weak actor, component;
    std::vector<ue::Weak> followers;
    Tag tag;
    std::string name;
    bool running{};
    bool ready{};
    CharacterModel model{CharacterModel::Runner};
    PoseClock pose_clock;
    float animation_length{};
    ProceduralPose procedural;
    struct WeaponPart
    {
        ue::Weak component;
        gait::BonePose attachment;
    };
    std::vector<WeaponPart> weapon_parts;
    unsigned weapon_kind{};
    std::string weapon_error;
};
} // namespace ghost
