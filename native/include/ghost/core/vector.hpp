#pragma once
#include <cmath>

namespace ghost
{
struct Vec3
{
    float x{}, y{}, z{};
};
inline Vec3 operator+(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(Vec3 a, float t)
{
    return {a.x * t, a.y * t, a.z * t};
}
inline float length(Vec3 a)
{
    return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}
inline float angle_delta(float a, float b)
{
    return std::remainder(b - a, 360.0f);
}
} // namespace ghost
