#include "ghost/animation/pose_constraints.hpp"
#include "ghost/animation/weapon_hold.hpp"
#include <iostream>

using namespace ghost;
using namespace ghost::gait;

void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

int main()
{
    try
    {
        require(std::abs(shotgun_pitch(-75, 0) + 32) < .001f &&
                    std::abs(shotgun_pitch(-75, 1) + 42) < .001f,
                "shotgun ready must stay forward of the knees and blend into aiming");
        for (const auto lengths :
             {std::array{25.0f, 25.0f}, std::array{38.0f, 29.0f}, std::array{17.0f, 34.0f}})
        {
            const ArmReach reach(lengths[0], lengths[1]);
            for (const Vec3 target :
                 {Vec3{}, Vec3{.001f, 0, 0}, Vec3{200, 30, -60}, Vec3{20, 5, 0}})
            {
                const auto safe = reach.project({}, target, {1, 0, 0});
                const auto pose = solve_limb({}, safe, {0, 0, -1}, reach.upper, reach.lower);
                const float bend = std::acos(
                    std::clamp(dot(unit(pose.joint), unit(pose.end - pose.joint)), -1.0f, 1.0f));
                require(bend <= 145.01f * pi / 180,
                        "an unreachable grip must not overfold the elbow");
                require(std::abs(length(pose.joint) - reach.upper) < .001f &&
                            std::abs(length(pose.end - pose.joint) - reach.lower) < .001f,
                        "reach adaptation must preserve both calibrated limb lengths");
                require(length(reach.project({}, safe, {1, 0, 0}) - safe) < .001f,
                        "a reachable target must be stable under repeated projection");
            }
        }
        for (Vec3 direction : {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 0, 1}, Vec3{0, 1, 0}})
            for (float turn : {-pi, -.5f, .5f, pi})
            {
                const auto limited =
                    wrist_cone(axis_angle({0, 0, 1}, turn), direction, direction, pi / 3);
                require(dot(rotate(limited, {1, 0, 0}), direction) >= .4999f,
                        "all gestures and antiparallel palms must stay inside the wrist cone");
            }
        for (float turn : {-pi, -1.5f, 1.5f, pi})
        {
            const auto limited =
                comfortable_upper_rotation(axis_angle({1, 0, 0}, turn), {1, 0, 0}, {1, 0, 0});
            const float angle = 2 * std::acos(std::clamp(std::abs(limited.w), 0.0f, 1.0f));
            require(angle <= .7501f,
                    "upper-arm roll must remain bounded independent of weapon type");
        }
        std::cout << "Pose constraints passed: adaptive reach, wrist cone and upper-arm roll\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
