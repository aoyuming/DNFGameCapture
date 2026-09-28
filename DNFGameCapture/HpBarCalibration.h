#pragma once
#include <algorithm>
#include <cmath>

namespace dnf::hp {
struct PixelRect { int left = 0, top = 0, right = 0, bottom = 0; };
enum class Handle { Whole, TopLeft, BottomRight };
inline int MinWidth(int width) { return (std::max)(20, static_cast<int>(std::ceil(width * 0.03))); }
inline int MinHeight(int height) { return (std::max)(2, static_cast<int>(std::ceil(height * 0.008))); }
inline bool HasFrame(int width, int height) { return width >= 20 && height >= 2; }
inline bool IsValid(const PixelRect& r, int width, int height) {
    return HasFrame(width, height) && r.left >= 0 && r.top >= 0 &&
        r.right <= width && r.bottom <= height && r.right > r.left && r.bottom > r.top &&
        r.right - r.left >= MinWidth(width) && r.bottom - r.top >= MinHeight(height);
}
// Right/bottom are exclusive. Use the same rounding for UI, sampling and INI round trips.
inline int ToPixel(float value, int extent) {
    if (extent <= 0 || !std::isfinite(value)) return 0;
    const double bounded = (std::max)(0.0, (std::min)(1.0, static_cast<double>(value)));
    return static_cast<int>(std::lround(bounded * extent));
}
inline PixelRect ToPixels(const float* r, int width, int height) {
    if (!r) return {};
    return { ToPixel(r[0], width), ToPixel(r[1], height), ToPixel(r[2], width), ToPixel(r[3], height) };
}
inline bool ToNormalized(const PixelRect& r, int width, int height, float* result) {
    if (!result || !IsValid(r, width, height)) return false;
    result[0] = static_cast<float>(r.left) / width;
    result[1] = static_cast<float>(r.top) / height;
    result[2] = static_cast<float>(r.right) / width;
    result[3] = static_cast<float>(r.bottom) / height;
    return true;
}
inline bool Nudge(PixelRect& r, int width, int height, Handle handle, int dx, int dy) {
    if (!IsValid(r, width, height)) return false;
    // Bound the delta before adding, including extreme/untrusted inputs.
    dx = (std::max)(-width, (std::min)(width, dx));
    dy = (std::max)(-height, (std::min)(height, dy));
    if (handle == Handle::Whole) {
        dx = (std::max)(-r.left, (std::min)(width - r.right, dx));
        dy = (std::max)(-r.top, (std::min)(height - r.bottom, dy));
        r.left += dx; r.right += dx; r.top += dy; r.bottom += dy;
    } else if (handle == Handle::TopLeft) {
        r.left = (std::max)(0, (std::min)(r.right - MinWidth(width), r.left + dx));
        r.top = (std::max)(0, (std::min)(r.bottom - MinHeight(height), r.top + dy));
    } else {
        r.right = (std::min)(width, (std::max)(r.left + MinWidth(width), r.right + dx));
        r.bottom = (std::min)(height, (std::max)(r.top + MinHeight(height), r.bottom + dy));
    }
    return true;
}
} // namespace dnf::hp
