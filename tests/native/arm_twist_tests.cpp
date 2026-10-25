#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include "ghost/animation/arm_twist.hpp"
#include "fixtures/gait_fixture.hpp"
#include <fstream>
#include <iostream>
using namespace ghost;
using namespace ghost::gait;
void require(bool ok, const char* label)
{
    if (!ok)
        throw std::runtime_error(label);
}
float angle(Quat a, Quat b)
{
    a = normalized(a);
    b = normalized(b);
    return 2 * std::acos(
                   std::clamp(std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w), 0.0f, 1.0f));
}
int main(int argc, char** argv)
{
    try
    {
        auto rest = gait_fixture();
        if (argc > 1)
        {
            std::ifstream f(argv[1]);
            for (auto& b : rest)
                require(bool(f >> b.p.x >> b.p.y >> b.p.z >> b.q.x >> b.q.y >> b.q.z >> b.q.w),
                        "body fixture");
        }
        Rig rig(rest);
        rest = rig.rest;
        std::array<BonePose, 6> refs;
        for (int side = 0; side < 2; ++side)
        {
            auto u = side == 0 ? UpperArmL : UpperArmR, l = side == 0 ? LowerArmL : LowerArmR,
                 h = side == 0 ? HandL : HandR;
            refs[side * 3] = {rest[u].p + (rest[l].p - rest[u].p) * .5f, rest[u].q};
            refs[side * 3 + 1] = {rest[l].p + (rest[h].p - rest[l].p) * (1.0f / 3), rest[l].q};
            refs[side * 3 + 2] = {rest[l].p + (rest[h].p - rest[l].p) * (2.0f / 3), rest[l].q};
        }
        if (argc > 2)
        {
            std::ifstream f(argv[2]);
            for (auto& b : refs)
                require(bool(f >> b.p.x >> b.p.y >> b.p.z >> b.q.x >> b.q.y >> b.q.z >> b.q.w),
                        "twist fixture");
        }
        ArmTwist twist(rig, refs);
        auto pose = rest;
        auto neutral = twist.apply(pose);
        for (int i = 0; i < 6; ++i)
        {
            require(length(neutral[i].p - refs[i].p) < .001f, "reference position");
            require(angle(neutral[i].q, refs[i].q) < .002f, "reference rotation");
        }
        for (int sign : {-1, 1})
            for (float rotation : {.3f, 1.2f, 2.0f})
            {
                pose = rest;
                const auto global = axis_angle(unit(Vec3{1, 2, 3}), .6f);
                const Vec3 shift{3, -7, 11};
                for (auto& b : pose)
                {
                    b.p = rotate(global, b.p) + shift;
                    b.q = global * b.q;
                }
                for (int side = 0; side < 2; ++side)
                {
                    auto l = side == 0 ? LowerArmL : LowerArmR, h = side == 0 ? HandL : HandR;
                    auto axis = unit(pose[h].p - pose[l].p);
                    pose[h].q = axis_angle(axis, sign * rotation) * pose[h].q;
                }
                const auto before = pose;
                const auto out = twist.apply(pose);
                for (std::size_t i = 0; i < Count; ++i)
                    require(length(pose[i].p - before[i].p) < .001f, "joint contact unchanged");
                for (int side = 0; side < 2; ++side)
                {
                    auto u = side == 0 ? UpperArmL : UpperArmR,
                         l = side == 0 ? LowerArmL : LowerArmR, h = side == 0 ? HandL : HandR;
                    auto axis = unit(pose[h].p - pose[l].p);
                    require(angle(pose[h].q, before[h].q) < .002f, "wrist grip unchanged");
                    require(angle(pose[l].q,
                                  axis_angle(axis, sign * rotation * .25f) * before[l].q) < .002f,
                            "forearm quarter roll");
                    for (int part = 0; part < 3; ++part)
                    {
                        int i = side * 3 + part;
                        float fraction = part == 0 ? 0 : part == 1 ? .5f : .8f;
                        require(length(out[i].p - (rotate(global, refs[i].p) + shift)) < .001f,
                                "twist follows parent");
                        require(angle(out[i].q, axis_angle(axis, sign * rotation * fraction) *
                                                    global * refs[i].q) < .002f,
                                "progressive twist");
                    }
                    require(angle(pose[u].q, before[u].q) < .002f, "upper arm unchanged");
                }
            }
        require(std::abs(axial_twist(axis_angle({1, 0, 0}, 3), {1, 0, 0}) - 2.0943951f) < .001f,
                "extreme roll bounded");
        const auto delta = limb_rotations({1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0});
        require(length(rotate(delta[0], {1, 0, 0}) - Vec3{0, 0, 1}) < .001f, "upper axis follows");
        require(length(rotate(delta[1], {0, 1, 0}) - Vec3{1, 0, 0}) < .001f, "lower axis follows");
        require(length(rotate(delta[0], {0, 0, 1}) - Vec3{0, 1, 0}) < .001f, "bend plane follows");
        std::cout << "arm twist and bend-plane tests passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
