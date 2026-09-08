#pragma once
#include <algorithm>
#include <array>
namespace dnf::preview {
struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;
    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
};
struct Layout { Rect game; std::array<Rect, 2> panels; };
inline Layout Calculate(int width, int height, int sourceWidth, int sourceHeight) {
    width = (std::max)(width, 32); height = (std::max)(height, 64);
    const int gap = 6;
    const int side = (std::min)(width / 2, (std::clamp)(width / 5, 196, 280));
    const int available = (std::max)(1, width - side - gap * 2);
    const double aspect = static_cast<double>((std::max)(1, sourceWidth)) / (std::max)(1, sourceHeight);
    const int gameWidth = (std::min)(available, static_cast<int>(height * aspect));
    const int gameHeight = (std::min)(height, (std::max)(1, static_cast<int>(gameWidth / aspect)));
    const int top = (height - gameHeight) / 2;
    const int panelHeight = (height - gap * 3) / 2;
    return {{0, top, gameWidth, top + gameHeight}, {{
        {gameWidth + gap, gap, width - gap, gap + panelHeight},
        {gameWidth + gap, gap * 2 + panelHeight, width - gap, height - gap}
    }}};
}
inline Rect Cell(const Rect& panel, int index) {
    const int gap = 5, title = 24;
    const int w = (panel.Width() - gap * 3) / 2;
    const int h = (panel.Height() - title - gap * 3) / 2;
    const int x = panel.left + gap + index % 2 * (w + gap);
    const int y = panel.top + title + gap + index / 2 * (h + gap);
    return {x, y, x + w, y + h};
}
}
