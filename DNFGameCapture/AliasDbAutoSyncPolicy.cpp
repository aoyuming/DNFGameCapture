#include "AliasDbAutoSyncPolicy.h"

bool DnfIsAliasAutoSyncDue(bool enabled, bool attemptedThisRun,
    std::int64_t lastSuccessAt, std::int64_t now)
{
    if (!enabled || attemptedThisRun) return false;
    if (lastSuccessAt <= 0) return true;
    if (now < lastSuccessAt) return true;
    return now - lastSuccessAt >= DNF_ALIAS_AUTO_SYNC_PERIOD_SECONDS;
}
