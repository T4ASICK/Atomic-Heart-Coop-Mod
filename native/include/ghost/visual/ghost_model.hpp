#pragma once

#include "ghost/core/state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace ghost
{
enum class CharacterModel
{
    Runner,
    StoryP3,
    ProceduralP3
};

inline std::optional<CharacterModel> character_model(std::string_view name)
{
    if (name == "runner")
        return CharacterModel::Runner;
    if (name == "story_p3")
        return CharacterModel::StoryP3;
    if (name == "procedural_p3")
        return CharacterModel::ProceduralP3;
    return {};
}

struct VisualMotion
{
    CharacterModel model;
    bool moving;
};
inline VisualMotion visual_motion(CharacterModel configured, float speed, bool was_moving)
{
    if (!std::isfinite(speed) || speed < 0)
        throw std::runtime_error("invalid visual movement speed");
    const bool moving = speed > (was_moving ? 15.0f : 35.0f);
    return {moving && configured == CharacterModel::StoryP3 ? CharacterModel::Runner : configured,
            moving};
}

struct ModelAssets
{
    std::string_view name;
    std::wstring_view skeleton, body, idle, walk;
    std::array<std::wstring_view, 5> followers;
    float reference_speed;
    bool manual_pose;
};

inline const ModelAssets& model_assets(CharacterModel model)
{
    static constexpr ModelAssets runner{
        "runner",
        L"/Game/GooseRunner/Core/AI/Characters/P3_Runner/SKEL_P3_Runner.SKEL_P3_Runner",
        L"/Game/GooseRunner/Core/AI/Characters/P3_Runner/SK_P3_Runner.SK_P3_Runner",
        L"/Game/GooseRunner/GooseRunner/Animations/AS_P3_Rest.AS_P3_Rest",
        L"/Game/GooseRunner/GooseRunner/Animations/AS_P3_RunForward.AS_P3_RunForward",
        {},
        450.0f,
        false};
    static constexpr ModelAssets story{
        "story_p3",
        L"/Game/Development/Characters/P3/SKEL_P3.SKEL_P3",
        L"/Game/Development/Characters/P3/SK_P3_NoHead.SK_P3_NoHead",
        L"/Game/Development/Characters/P3/Animation/SC_01-57/"
        L"AS_P3_SC_01-57_0120_idle.AS_P3_SC_01-57_0120_idle",
        {}, // Movement switches to the runner's complete mesh and animation.
        {L"/Game/Development/Characters/P3/SK_P3_Face.SK_P3_Face",
         L"/Game/Development/Characters/P3/SK_P3_Hairs.SK_P3_Hairs",
         L"/Game/Development/Characters/P3/SK_P3_Beard.SK_P3_Beard",
         L"/Game/Development/Characters/P3/SK_P3_Brows.SK_P3_Brows",
         L"/Game/Development/Characters/P3/SK_P3_Lashes.SK_P3_Lashes"},
        160.0f,
        true};
    static constexpr ModelAssets procedural{"procedural_p3", story.skeleton, story.body, {}, {},
                                            story.followers, 160.0f,         false};
    if (model == CharacterModel::ProceduralP3)
        return procedural;
    return model == CharacterModel::StoryP3 ? story : runner;
}

// RootBoneTranslation is the evaluated offset from the reference pose in
// component space. Counter-translate the ghost actor, not the shared animation.
inline Vec3 story_mesh_origin(Vec3 anchor, float mesh_yaw, Vec3 root_offset)
{
    if (!std::isfinite(root_offset.x) || !std::isfinite(root_offset.y) ||
        !std::isfinite(root_offset.z) || !std::isfinite(mesh_yaw) || length(root_offset) > 2000)
        throw std::runtime_error("invalid story P3 root displacement");
    const float radians = mesh_yaw * 0.017453292519943295f;
    const float c = std::cos(radians), s = std::sin(radians);
    return anchor - Vec3{c * root_offset.x - s * root_offset.y,
                         s * root_offset.x + c * root_offset.y, root_offset.z};
}

// Cutscene clips are sampled explicitly while stopped. Never auto-play their
// animation notifies on a visual-only ghost. Time is independent per visual.
class PoseClock
{
    std::optional<double> previous;
    double position{};

public:
    void reset()
    {
        previous.reset();
        position = 0;
    }
    float advance(double now, float rate, float duration)
    {
        if (!std::isfinite(now) || !std::isfinite(rate) || rate < 0 || !std::isfinite(duration) ||
            duration <= 0 || duration > 3600)
            throw std::runtime_error("invalid visual animation timing");
        const double delta = previous ? std::clamp(now - *previous, 0.0, 0.25) : 0.0;
        previous = now;
        position = std::fmod(position + delta * rate, static_cast<double>(duration));
        return static_cast<float>(position);
    }
};
} // namespace ghost
