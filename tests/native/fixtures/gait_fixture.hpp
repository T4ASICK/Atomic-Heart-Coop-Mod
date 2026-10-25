#pragma once
#include <array>
#include <cstddef>
#include "ghost/animation/procedural_gait.hpp"

// Synthetic proportions for portable tests. No game reference transforms here.
inline ghost::gait::Pose gait_fixture()
{
    using namespace ghost::gait;
    Pose pose{};
    const std::array<ghost::Vec3, Count> positions{
        {{0, 0, 95},   {0, 0, 105},   {0, 0, 120},   {0, 0, 140},    {0, 0, 150},   {0, 0, 165},
         {0, -7, 140}, {0, -16, 140}, {5, -41, 132}, {12, -63, 120}, {0, 7, 140},   {0, 16, 140},
         {5, 41, 132}, {12, 63, 120}, {0, -12, 93},  {4, -14, 52},   {-3, -16, 10}, {14, -16, 3},
         {0, 12, 93},  {4, 14, 52},   {-3, 16, 10},  {14, 16, 3}}};
    for (std::size_t i = 0; i < Count; ++i)
        pose[i].p = positions[i];
    return pose;
}
