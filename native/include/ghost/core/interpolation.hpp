#pragma once
#include <cstddef>
#include "ghost/core/state.hpp"
#include <algorithm>
#include <deque>
#include <optional>
#include <utility>
#include <map>

namespace ghost
{
// Arrival time belongs to a unique peer sample, independently of visual history.
// Keep the watermark through empty rosters and local world/visibility changes.
class RosterFeed
{
    std::map<std::string, State> last_samples;

public:
    Roster roster;
    double heartbeat{};
    bool push(Roster next, double now)
    {
        if (next.session == roster.session && next.seq <= roster.seq)
            return false;
        for (auto& state : next.players)
        {
            auto previous = last_samples.find(state.id);
            const bool changed = previous == last_samples.end() ||
                                 state.session != previous->second.session ||
                                 state.world != previous->second.world;
            if (changed || state.seq > previous->second.seq)
            {
                state.received_at = now;
                last_samples[state.id] = state;
            }
            else
                state = previous->second;
        }
        // Keep recent watermarks across brief absences without unbounded churn.
        while (last_samples.size() > 64)
        {
            auto oldest = std::min_element(last_samples.begin(), last_samples.end(),
                [](const auto& a, const auto& b)
                { return a.second.received_at < b.second.received_at; });
            last_samples.erase(oldest);
        }
        roster = std::move(next);
        heartbeat = now;
        return true;
    }
};

class Track
{
    struct Sample
    {
        State state;
        double time;
    };
    std::deque<Sample> samples;

public:
    void clear()
    {
        samples.clear();
    }
    bool push(const State& state, double now)
    {
        if (!samples.empty())
        {
            const auto& previous = samples.back().state;
            if (state.session != previous.session || state.world != previous.world ||
                state.id != previous.id)
                clear();
            else if (state.seq <= previous.seq)
                return false;
            else if (length(state.position - previous.position) > 1500)
                clear();
        }
        samples.push_back({state, now});
        while (samples.size() > 32)
            samples.pop_front();
        return true;
    }
    bool stale(double now) const
    {
        return samples.empty() || now - samples.back().time > 2.0;
    }
    std::optional<State> latest(double now, double max_age = 0.75) const
    {
        if (samples.empty() || now < samples.back().time || now - samples.back().time > max_age)
            return {};
        return samples.back().state;
    }
    std::optional<State> evaluate(double now) const
    {
        if (stale(now))
            return {};
        const double target = now - 0.1;
        if (target <= samples.front().time)
            return samples.front().state;
        for (std::size_t i = 1; i < samples.size(); ++i)
        {
            const auto& a = samples[i - 1];
            const auto& b = samples[i];
            if (target > b.time)
                continue;
            const float t =
                static_cast<float>((target - a.time) / std::max(0.0001, b.time - a.time));
            State s = b.state;
            s.position = a.state.position + (b.state.position - a.state.position) * t;
            s.yaw = a.state.yaw + angle_delta(a.state.yaw, b.state.yaw) * t;
            return s;
        }
        State s = samples.back().state;
        const float ahead = static_cast<float>(std::clamp(target - samples.back().time, 0.0, 0.15));
        s.position = s.position + s.velocity * ahead;
        if (target - samples.back().time > 0.15)
            s.velocity = {};
        return s;
    }
};
} // namespace ghost
