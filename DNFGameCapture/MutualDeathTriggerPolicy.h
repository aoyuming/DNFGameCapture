#pragma once
#include <cstdint>

namespace dnf::ocr {
// UI-thread-only admission policy. Does not change the scoring deduplicator.
// Stable X observations, rather than OCR completion times, define the pair.
class MutualDeathTriggerWindow {
public:
    static constexpr std::uint32_t WindowMs = 1500;

    void Reset() noexcept { firstSide_ = -1; firstTick_ = 0; consumed_ = false; }
    void Begin(int deadSide, std::uint32_t tick) noexcept {
        Reset();
        if (deadSide == 0 || deadSide == 1) {
            firstSide_ = deadSide;
            firstTick_ = tick;
        }
    }
    void Observe(bool leftDead, bool rightDead) noexcept {
        if ((firstSide_ == 0 && !leftDead) || (firstSide_ == 1 && !rightDead)) Reset();
    }
    // Caller must additionally require a NEW stable death edge on deadSide.
    // A round-end cooldown must not discard the other half of an already
    // admitted pair, but is never itself allowed to open this window.
    bool ConsumeReciprocal(int deadSide, std::uint32_t tick,
        bool leftDead, bool rightDead) noexcept {
        if (firstSide_ < 0 || consumed_ || (deadSide != 0 && deadSide != 1) ||
            deadSide == firstSide_ || !leftDead || !rightDead ||
            static_cast<std::uint32_t>(tick - firstTick_) > WindowMs) return false;
        consumed_ = true;
        return true;
    }
private:
    int firstSide_ = -1;
    std::uint32_t firstTick_ = 0;
    bool consumed_ = false;
};
} // namespace dnf::ocr
