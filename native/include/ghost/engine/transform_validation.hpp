#pragma once
#include <cmath>

namespace ghost
{
// UE4 FTransform uses four-float SIMD storage for FVector scale. The fourth
// lane is padding (normally zero), not a fourth spatial scale component.
inline bool unit_xyz_scale(const float (&scale)[4])
{
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(scale[axis]) || std::abs(scale[axis] - 1) > .01f)
            return false;
    return true;
}
} // namespace ghost
