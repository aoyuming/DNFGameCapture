#pragma once

#include <cstdint>

inline constexpr std::int64_t DNF_ALIAS_AUTO_SYNC_PERIOD_SECONDS =
    7LL * 24LL * 60LL * 60LL;

// Returns true only when the caller is allowed to start this run's attempt.
// The caller owns the attemptedThisRun flag and should clear it on process
// startup or when the user explicitly re-enables the feature.
bool DnfIsAliasAutoSyncDue(bool enabled, bool attemptedThisRun,
    std::int64_t lastSuccessAt, std::int64_t now);
