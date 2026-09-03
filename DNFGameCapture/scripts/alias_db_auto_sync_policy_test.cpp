#include "../AliasDbAutoSyncPolicy.h"

#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void TestFirstRunIsDue()
{
    Require(DnfIsAliasAutoSyncDue(true, false, 0, 1),
        "a missing success timestamp must run immediately");
}

void TestSevenDayBoundary()
{
    constexpr std::int64_t lastSuccess = 1'700'000'000;
    Require(!DnfIsAliasAutoSyncDue(true, false, lastSuccess,
        lastSuccess + DNF_ALIAS_AUTO_SYNC_PERIOD_SECONDS - 1),
        "the task must wait until the full seven-day period");
    Require(DnfIsAliasAutoSyncDue(true, false, lastSuccess,
        lastSuccess + DNF_ALIAS_AUTO_SYNC_PERIOD_SECONDS),
        "the task must run at the seven-day boundary");
}

void TestDisabledAndFailedRunAreQuiet()
{
    Require(!DnfIsAliasAutoSyncDue(false, false, 0, 1),
        "a disabled task must not run");
    Require(!DnfIsAliasAutoSyncDue(true, true, 0, 1),
        "a task that already attempted and failed this run must not retry");
}

void TestClockRollbackRuns()
{
    constexpr std::int64_t lastSuccess = 1'700'000'000;
    Require(DnfIsAliasAutoSyncDue(true, false, lastSuccess, lastSuccess - 1),
        "a clock rollback must be treated as due");
}

void TestReenabledTaskCanRun()
{
    std::int64_t lastSuccessAt = 1'700'000'000;
    bool attemptedThisRun = true;
    // The UI handler resets these two values when the user turns the option
    // back on, so the next policy check is an intentional immediate attempt.
    lastSuccessAt = 0;
    attemptedThisRun = false;
    Require(DnfIsAliasAutoSyncDue(true, attemptedThisRun, lastSuccessAt,
        1'700'000'001),
        "re-enabling a task with a fresh run generation must allow an immediate run");
}

} // namespace

int main()
{
    TestFirstRunIsDue();
    TestSevenDayBoundary();
    TestDisabledAndFailedRunAreQuiet();
    TestClockRollbackRuns();
    TestReenabledTaskCanRun();
    std::cout << "Alias DB auto-sync policy tests passed.\n";
    return 0;
}
