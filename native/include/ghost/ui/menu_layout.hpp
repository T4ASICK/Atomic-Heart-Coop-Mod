#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace ghost
{
inline std::optional<float> menu_render_scale(float width, float height, float dpi)
{
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(dpi) || dpi < .1f ||
        dpi > 8 || width <= 48 || height <= 48)
        return {};
    const float fit = std::min(
        {std::clamp(height / 1080.0f, .85f, 1.5f), (width - 48) / 600, (height - 48) / 640});
    return fit / dpi;
}
} // namespace ghost
