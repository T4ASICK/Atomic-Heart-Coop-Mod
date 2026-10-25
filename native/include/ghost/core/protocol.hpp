#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <vector>
#include "ghost/core/state.hpp"
#include <algorithm>
#include <charconv>
#include <optional>
#include <string_view>
#include <utility>

namespace ghost
{
inline std::vector<std::string_view> split(std::string_view s, char separator)
{
    std::vector<std::string_view> result;
    for (;;)
    {
        const auto end = s.find(separator);
        result.push_back(s.substr(0, end));
        if (end == s.npos)
            return result;
        s.remove_prefix(end + 1);
    }
}
inline bool token(std::string_view s)
{
    return !s.empty() && s.size() <= 64 &&
           std::all_of(s.begin(), s.end(),
                       [](char c)
                       {
                           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                  (c >= '0' && c <= '9') || c == '_' || c == '-';
                       });
}
inline bool printable(std::string_view s, std::size_t maximum)
{
    return !s.empty() && s.size() <= maximum &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
inline bool number(std::string_view text, float& value, float bound)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() && std::isfinite(value) &&
           std::abs(value) <= bound;
}
inline bool sequence(std::string_view text, std::uint64_t& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() && value > 0 &&
           value < (1ULL << 53);
}
inline std::optional<Roster> parse_roster(std::string_view text)
{
    const auto bounded_unsigned = [](std::string_view value, auto& output, std::uint64_t maximum)
    {
        const auto [end, error] =
            std::from_chars(value.data(), value.data() + value.size(), output);
        return error == std::errc{} && end == value.data() + value.size() && output <= maximum;
    };
    if (text.size() > 8192)
        return {};
    if (!text.empty() && text.back() == '\n')
        text.remove_suffix(1);
    auto lines = split(text, '\n');
    auto header = split(lines[0], '\t');
    Roster result;
    if (header.size() != 4 || header[0] != "AHGR3" || !token(header[1]) ||
        !sequence(header[2], result.seq) || (header[3] != "0" && header[3] != "1"))
        return {};
    const std::size_t count = header[3] == "1" ? 1 : 0;
    if (lines.size() != count + 1)
        return {};
    result.session = header[1];
    for (std::size_t i = 1; i < lines.size(); ++i)
    {
        auto f = split(lines[i], '\t');
        State s;
        if (f.size() != 37 || !token(f[0]) || !printable(f[1], 96) || !token(f[2]) ||
            !sequence(f[3], s.seq) || !f[4].starts_with("/Game/") || !printable(f[4], 256) ||
            !number(f[5], s.position.x, 1e8f) || !number(f[6], s.position.y, 1e8f) ||
            !number(f[7], s.position.z, 1e8f) || !number(f[8], s.yaw, 36000) ||
            !number(f[9], s.velocity.x, 1e5f) || !number(f[10], s.velocity.y, 1e5f) ||
            !number(f[11], s.velocity.z, 1e5f) || !bounded_unsigned(f[12], s.combat.weapon, 13) ||
            !number(f[13], s.combat.pitch, 89) || !bounded_unsigned(f[14], s.combat.flags, 255) ||
            !bounded_unsigned(f[15], s.combat.ability, 8) ||
            !bounded_unsigned(f[16], s.combat.shot, 0xffffffffULL) ||
            !bounded_unsigned(f[17], s.combat.action, 0xffffffffULL))
            return {};
        if (f[18] != "0" && f[18] != "1")
            return {};
        s.combat.calibrated = f[18] == "1";
        for (std::size_t j = 0; j < 18; ++j)
        {
            const float bound = j < 3 ? 200.0f : (j >= 7 && j < 10 ? 150.0f : 1.001f);
            if (!number(f[19 + j], s.combat.grip[j], bound) ||
                (!s.combat.calibrated && s.combat.grip[j] != 0))
                return {};
        }
        if (s.combat.calibrated)
            for (const auto start : {3U, 10U, 14U})
            {
                double norm = 0;
                for (unsigned j = 0; j < 4; ++j)
                    norm +=
                        static_cast<double>(s.combat.grip[start + j]) * s.combat.grip[start + j];
                if (norm < 0.98 || norm > 1.02)
                    return {};
            }
        s.id = f[0];
        s.name = f[1];
        s.session = f[2];
        s.world = f[4];
        result.players.push_back(std::move(s));
    }
    return result;
}
} // namespace ghost
