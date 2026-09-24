#include "../MutualDeathTriggerPolicy.h"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
using dnf::ocr::MutualDeathTriggerWindow;
int main() {
    try {
        MutualDeathTriggerWindow window;
        CHECK(!window.ConsumeReciprocal(1, 1000, true, true)); // round cooldown alone cannot admit
        window.Begin(0, 1000);
        CHECK(!window.ConsumeReciprocal(0, 1000, true, true)); // same-side repeat
        CHECK(window.ConsumeReciprocal(1, 1000, true, true)); // same frame, L then R
        CHECK(!window.ConsumeReciprocal(1, 1001, true, true)); // at most one reciprocal
        CHECK(!window.ConsumeReciprocal(0, 1001, true, true));
        window.Begin(1, 2000);
        CHECK(window.ConsumeReciprocal(0, 2240, true, true)); // R then L, next poll
        window.Begin(0, 3000);
        CHECK(!window.ConsumeReciprocal(1, 3240, true, false)); // not stable on both sides
        CHECK(window.ConsumeReciprocal(1, 3480, true, true)); // delayed stable sample
        window.Begin(0, 4000);
        CHECK(window.ConsumeReciprocal(1, 5500, true, true)); // inclusive boundary
        window.Begin(0, 4000);
        CHECK(!window.ConsumeReciprocal(1, 5501, true, true));
        CHECK(!window.ConsumeReciprocal(1, 14000, true, true)); // 10-second queue not a pair
        window.Begin(0, 1000);
        window.Observe(false, true);
        CHECK(!window.ConsumeReciprocal(1, 1240, true, true)); // first side recovered
        window.Begin(1, 1000);
        window.Observe(true, false);
        CHECK(!window.ConsumeReciprocal(0, 1240, true, true));
        window.Begin(0, 1000);
        window.Observe(true, false);
        window.Observe(true, true);
        CHECK(window.ConsumeReciprocal(1, 1480, true, true));
        window.Reset();
        CHECK(!window.ConsumeReciprocal(1, 1480, true, true)); // stop/reset invalidates
        window.Begin(-1, 1000);
        CHECK(!window.ConsumeReciprocal(1, 1000, true, true));
        window.Begin(0, 1000);
        CHECK(!window.ConsumeReciprocal(2, 1000, true, true));
        CHECK(!window.ConsumeReciprocal(-1, 1000, true, true));
        window.Begin(0, 0xfffffff0u);
        CHECK(window.ConsumeReciprocal(1, 0x20u, true, true)); // GetTickCount wrap
        window.Begin(0, 0xfffffff0u);
        CHECK(!window.ConsumeReciprocal(1, 0x1000u, true, true));
        window.Begin(1, 0); // tick zero is valid, no sentinel collision
        CHECK(window.ConsumeReciprocal(0, 0, true, true));
        std::cout << "Mutual death trigger policy: all checks passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
