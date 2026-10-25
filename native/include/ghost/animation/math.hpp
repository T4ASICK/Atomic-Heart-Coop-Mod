#pragma once
#include <cmath>
#include "ghost/core/vector.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace ghost::gait
{
constexpr float pi = 3.14159265358979323846f;
inline float dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline bool valid_vector(Vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline Vec3 unit(Vec3 v)
{
    const float size = length(v);
    if (!valid_vector(v) || !std::isfinite(size) || size < 1e-5f)
        throw std::runtime_error("degenerate procedural rig vector");
    return v * (1.0f / size);
}
struct Quat
{
    float x{}, y{}, z{}, w{1};
};
inline Quat inverse(Quat q)
{
    return {-q.x, -q.y, -q.z, q.w};
}
inline Quat normalized(Quat q)
{
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!std::isfinite(n) || n < 1e-5f)
        throw std::runtime_error("invalid procedural bone rotation");
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}
inline Quat operator*(Quat a, Quat b)
{
    return normalized({a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                       a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                       a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                       a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z});
}
inline Vec3 rotate(Quat q, Vec3 v)
{
    const Vec3 t = cross({q.x, q.y, q.z}, v) * 2;
    return v + t * q.w + cross({q.x, q.y, q.z}, t);
}
inline Quat axis_angle(Vec3 axis, float radians)
{
    axis = unit(axis);
    const float s = std::sin(radians * 0.5f);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(radians * 0.5f)};
}
inline Quat from_to(Vec3 a, Vec3 b)
{
    a = unit(a);
    b = unit(b);
    const float d = std::clamp(dot(a, b), -1.0f, 1.0f);
    if (d < -0.9999f)
    {
        Vec3 axis = cross(a, std::abs(a.x) < 0.8f ? Vec3{1, 0, 0} : Vec3{0, 1, 0});
        return axis_angle(axis, pi);
    }
    const auto c = cross(a, b);
    return normalized({c.x, c.y, c.z, 1 + d});
}
inline Quat frame_rotation(Vec3 forward, Vec3 thumb)
{
    const auto x = unit(forward), y = unit(thumb - x * dot(thumb, x)), z = cross(x, y);
    const float trace = x.x + y.y + z.z;
    Quat q;
    if (trace > 0)
    {
        const float s = std::sqrt(trace + 1) * 2;
        q = {(y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, s * 0.25f};
    }
    else if (x.x > y.y && x.x > z.z)
    {
        const float s = std::sqrt(1 + x.x - y.y - z.z) * 2;
        q = {s * 0.25f, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s};
    }
    else if (y.y > z.z)
    {
        const float s = std::sqrt(1 + y.y - x.x - z.z) * 2;
        q = {(y.x + x.y) / s, s * 0.25f, (z.y + y.z) / s, (z.x - x.z) / s};
    }
    else
    {
        const float s = std::sqrt(1 + z.z - x.x - y.y) * 2;
        q = {(z.x + x.z) / s, (z.y + y.z) / s, s * 0.25f, (x.y - y.x) / s};
    }
    return normalized(q);
}
inline std::array<Quat, 2> limb_rotations(Vec3 rest_upper, Vec3 rest_lower, Vec3 upper, Vec3 lower)
{
    const auto a = unit(rest_upper), b = unit(rest_lower), c = unit(upper), d = unit(lower);
    const auto rest_normal = cross(a, b), normal = cross(c, d);
    // Near-straight chains have no stable bend plane; retain the minimal swing.
    if (length(rest_normal) < 0.02f || length(normal) < 0.02f)
        return {from_to(a, c), from_to(b, d)};
    const auto n0 = unit(rest_normal), n = unit(normal);
    return {frame_rotation(c, n) * inverse(frame_rotation(a, n0)),
            frame_rotation(d, n) * inverse(frame_rotation(b, n0))};
}

inline Quat blend_rotation(Quat a, Quat b, float t)
{
    const float sign = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0 ? -1.0f : 1.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return normalized({a.x * (1 - t) + b.x * t * sign, a.y * (1 - t) + b.y * t * sign,
                       a.z * (1 - t) + b.z * t * sign, a.w * (1 - t) + b.w * t * sign});
}
} // namespace ghost::gait
