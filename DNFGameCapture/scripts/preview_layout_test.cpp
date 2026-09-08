#include "../PreviewLayout.h"
#include <cassert>
#include <iostream>
int main() {
    for (const auto width : {640, 900, 1180, 1920}) for (const auto height : {200, 544, 900}) {
        const auto layout = dnf::preview::Calculate(width, height, 1920, 1080);
        assert(layout.game.left == 0 && layout.game.right <= layout.panels[0].left);
        assert(layout.game.Width() > 0 && layout.game.Height() > 0);
        assert(layout.panels[0].bottom < layout.panels[1].top);
        for (const auto& panel : layout.panels) {
            assert(panel.right <= width && panel.bottom <= height);
            for (int i = 0; i < 4; ++i) {
                auto cell = dnf::preview::Cell(panel, i);
                assert(cell.left >= panel.left && cell.right <= panel.right);
                assert(cell.top >= panel.top + 20 && cell.bottom <= panel.bottom);
            }
        }
    }
    std::cout << "Preview layout checks passed\n";
}
