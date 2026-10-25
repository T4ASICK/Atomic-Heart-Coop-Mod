#pragma once
#include "ghost/visual/visual_state.hpp"
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>

namespace ghost
{
// Opt-in QA readback. Requested transforms and engine reads remain distinct.
// Sampling is bounded to 600 pairs at 10 Hz, with disk I/O outside the game thread.
class PoseProbe
{
    using Call = ue::Reflection::Call;
    double previous{-1};
    unsigned samples{};
    bool failed{};
    std::array<std::string, gait::Count + 6 + 30> names;

    static void vector(std::ostream& out, Vec3 v)
    {
        out << '[' << v.x << ',' << v.y << ',' << v.z << ']';
    }

public:
    struct Sample
    {
        std::string text;
        bool first{};
    };
    PoseProbe()
    {
        for (std::size_t i = 0; i < gait::Count; ++i)
            names[i] = ue::utf8(gait::bone_names[i]);
        for (std::size_t i = 0; i < gait::twist_names.size(); ++i)
            names[gait::Count + i] = ue::utf8(gait::twist_names[i]);
        std::size_t index = gait::Count + 6;
        for (const auto* side : {"l", "r"})
            for (const auto* finger : {"index", "middle", "ring", "pinky", "thumb"})
                for (int joint = 1; joint <= 3; ++joint)
                    names[index++] =
                        std::string(finger) + "_0" + std::to_string(joint) + '_' + side;
    }
    void reset()
    {
        previous = -1;
        samples = 0;
        failed = false;
    }
    void disable()
    {
        failed = true;
    }
    std::optional<Sample> sample(ue::Api& api, ue::Reflection& reflection, const Visual& visual,
                                 const State& state, Vec3 expected_origin, float expected_yaw,
                                 double now, std::uint64_t generation)
    {
        const auto& pose = visual.procedural;
        if (failed || samples >= 600 || (previous >= 0 && now - previous < .1) ||
            !pose.requested_pose_valid || visual.model != CharacterModel::ProceduralP3)
            return {};
        auto* component = api.weak_get(&visual.component);
        if (!component)
            return {};
        previous = now;
        std::array<Vec3, gait::Count + 6 + 30> actual;
        std::array<gait::Quat, gait::Count + 6 + 30> actual_rotations;
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            const ue::Name bone =
                i < gait::Count ? pose.bones[i]
                : i < gait::Count + 6
                    ? pose.twist_bones[i - gait::Count]
                    : pose.hands[(i - gait::Count - 6) / 15].bones[(i - gait::Count - 6) % 15];
            Call read(reflection, L"/Script/Engine.PoseableMeshComponent:GetBoneTransformByName");
            read.set(L"BoneName", bone)
                .set(L"BoneSpace", static_cast<unsigned char>(0))
                .run(component);
            const auto transform = read.get<ue::Transform>();
            actual[i] = {transform.translation[0], transform.translation[1],
                         transform.translation[2]};
            actual_rotations[i] = gait::normalized({transform.rotation[0], transform.rotation[1],
                                                    transform.rotation[2], transform.rotation[3]});
            if (!gait::valid_vector(actual[i]))
                throw std::runtime_error("nonfinite pose probe readback");
        }
        Call root(reflection, L"/Script/Engine.SceneComponent:GetSocketTransform");
        root.set(L"InSocketName", ue::Name{})
            .set(L"TransformSpace", static_cast<unsigned char>(0))
            .run(component);
        const auto world = root.get<ue::Transform>();
        const Vec3 actual_origin{world.translation[0], world.translation[1], world.translation[2]};
        if (!gait::valid_vector(actual_origin))
            throw std::runtime_error("nonfinite pose probe origin");
        for (float axis : world.rotation)
            if (!std::isfinite(axis))
                throw std::runtime_error("nonfinite pose probe rotation");
        const auto turn = gait::axis_angle({0, 0, 1}, expected_yaw * gait::pi / 180);
        float maximum_position_error = 0, maximum_rotation_error = 0;
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            maximum_position_error = std::max(
                maximum_position_error,
                length(expected_origin + gait::rotate(turn, pose.requested_pose[i].p) - actual[i]));
            const auto expected_rotation = turn * pose.requested_pose[i].q;
            const auto actual_rotation = actual_rotations[i];
            const float cosine = std::abs(
                expected_rotation.x * actual_rotation.x + expected_rotation.y * actual_rotation.y +
                expected_rotation.z * actual_rotation.z + expected_rotation.w * actual_rotation.w);
            maximum_rotation_error =
                std::max(maximum_rotation_error,
                         2 * std::acos(std::clamp(cosine, 0.0f, 1.0f)) * 180 / gait::pi);
        }
        const float actual_yaw = std::atan2(2 * (world.rotation[3] * world.rotation[2] +
                                                 world.rotation[0] * world.rotation[1]),
                                            1 - 2 * (world.rotation[1] * world.rotation[1] +
                                                     world.rotation[2] * world.rotation[2])) *
                                 180 / gait::pi;
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(9);
        for (bool expected : {true, false})
        {
            out << "{\"v\":1,\"source\":\"live_engine_readback\",\"machine\":\""
                << (expected ? "expected" : "applied") << "\",\"t_ms\":" << now * 1000
                << ",\"readback_error\":{\"position_cm\":" << maximum_position_error
                << ",\"rotation_deg\":" << maximum_rotation_error
                << "},\"frame\":" << pose.pose_sequence
                << ",\"actors\":[{\"ref\":" << (expected ? 1 : 2) << ",\"cell\":" << generation
                << ",\"valid3d\":true,\"dead\":false,\"pos\":";
            vector(out, expected ? expected_origin : actual_origin);
            out << ",\"rot\":[0,0," << (expected ? expected_yaw : actual_yaw) << "],\"bones\":{";
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (i)
                    out << ',';
                out << '"' << names[i] << "\":";
                vector(out, expected
                                ? expected_origin + gait::rotate(turn, pose.requested_pose[i].p)
                                : actual[i]);
            }
            out << "},\"requested_flags\":" << state.combat.flags << "}]}\n";
        }
        const bool first = samples++ == 0;
        return Sample{out.str(), first};
    }
};
} // namespace ghost
