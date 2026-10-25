#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include "fixtures/gait_fixture.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace ghost;
using namespace ghost::gait;
void require(bool ok, const char* label)
{
    if (!ok)
        throw std::runtime_error(label);
}
int main(int argc, char** argv)
{
    try
    {
        auto body = gait_fixture();
        if (argc > 1)
        {
            std::ifstream f(argv[1]);
            for (auto& b : body)
                require(bool(f >> b.p.x >> b.p.y >> b.p.z >> b.q.x >> b.q.y >> b.q.z >> b.q.w),
                        "invalid body fixture");
        }
        for (const auto clip : {authored::idle, authored::walk, authored::jog, authored::sprint})
        {
            require(std::abs(clip.count / clip.duration - 30) < .01f,
                    "authored clips retain their source rate");
            float motion = 0;
            for (std::size_t i = 0; i < clip.count; ++i)
            {
                const auto& a = clip.frames[i];
                const auto& b = clip.frames[(i + 1) % clip.count];
                motion += length(a.offset[Pelvis] - b.offset[Pelvis]);
                for (std::size_t bone = 0; bone < Count; ++bone)
                {
                    const auto q = a.rotation[bone];
                    require(valid_vector(a.offset[bone]), "finite authored sample");
                    require(std::abs(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1) < .001f,
                            "normalized authored rotations");
                }
            }
            require(motion > .002f, "source motion has living whole-body movement");
            const auto seam0 = sample_motion(clip, 1 - 1e-6), seam1 = sample_motion(clip, 0);
            require(length(seam0.offset[Pelvis] - seam1.offset[Pelvis]) < .001f,
                    "continuous loop interpolation");
        }
        float worst_length = 0, worst_floor = 0, worst_pelvis_step = 0;
        for (float scale : {.8f, 1.0f, 1.2f})
            for (Vec3 velocity :
                 {Vec3{}, Vec3{90, 0, 0}, Vec3{160, 0, 0}, Vec3{-160, 0, 0}, Vec3{0, 160, 0},
                  Vec3{0, -160, 0}, Vec3{350, 0, 0}, Vec3{450, 0, 0}})
            {
                auto reference = body;
                for (auto& b : reference)
                    b.p = b.p * scale;
                Rig rig(reference);
                Animator gait;
                Pose before{};
                for (int i = 0; i < 480; ++i)
                {
                    auto pose = gait.evaluate(rig, velocity, i / 60.0);
                    for (const auto chain :
                         {std::array{ThighL, CalfL, FootL}, std::array{ThighR, CalfR, FootR},
                          std::array{UpperArmL, LowerArmL, HandL},
                          std::array{UpperArmR, LowerArmR, HandR}})
                        for (int j = 0; j < 2; ++j)
                            worst_length = std::max(
                                worst_length,
                                std::abs(length(pose[chain[j + 1]].p - pose[chain[j]].p) -
                                         length(rig.rest[chain[j + 1]].p - rig.rest[chain[j]].p)));
                    const float sole =
                        std::min(sole_height(rig, pose, FootL), sole_height(rig, pose, FootR));
                    require(sole > -.01f, "retargeted sole remains above the floor");
                    if (gait.has_support())
                        worst_floor = std::max(worst_floor, std::abs(sole));
                    if (i > 90)
                    {
                        const float step = length(pose[Pelvis].p - before[Pelvis].p) / scale;
                        worst_pelvis_step = std::max(worst_pelvis_step, step);
                        require(step < 6, "pelvis has no one-frame teleports");
                        require(dot(pose[FootR].p - pose[FootL].p, rig.right) > 3 * scale,
                                "feet retain left/right order");
                    }
                    before = pose;
                }
            }
        require(worst_length < .005f, "authored retargeting preserves limb lengths at all scales");
        require(worst_floor < .01f, "heel/toe support is retained during stance");
        std::cout << "Authored motion tests passed: limb error " << worst_length << "cm; support "
                  << worst_floor << "cm; max pelvis step " << worst_pelvis_step << "cm at 60Hz\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
