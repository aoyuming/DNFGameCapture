#include "../LicenseLease.h"

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

DnfLicenseLeaseRecord MakeLease()
{
    DnfLicenseLeaseRecord lease;
    lease.licenseKey = L"CDK-278D00-ABC-1234";
    lease.machineId = L"machine-a";
    lease.cloudServerUrl = L"http://47.109.149.111:18880";
    lease.cardDuration = DNF_LICENSE_MONTH_SECONDS;
    lease.expireTime = 2'000'000'000;
    lease.validatedAt = 1'700'000'000;
    lease.lastUsedAt = lease.validatedAt;
    return lease;
}

void TestEligibilityBoundary()
{
    Require(!DnfIsLicenseLeaseEligible(DNF_LICENSE_MONTH_SECONDS - 1),
        "cards shorter than a month must verify every launch");
    Require(DnfIsLicenseLeaseEligible(DNF_LICENSE_MONTH_SECONDS),
        "monthly cards must be lease eligible");
    Require(DnfIsLicenseLeaseEligible(DNF_LICENSE_PERMANENT_EXPIRY),
        "permanent cards must be lease eligible");
}

void TestFiveDayWindow()
{
    auto lease = MakeLease();
    const std::int64_t deadline = lease.validatedAt + DNF_LICENSE_LEASE_SECONDS;
    Require(DnfGetLicenseLeaseValidUntil(lease) == deadline,
        "normal leases must expire five days after validation");
    Require(DnfValidateLicenseLease(lease, lease.licenseKey, lease.machineId,
        lease.cardDuration, deadline - 1) == DnfLicenseLeaseValidation::valid,
        "lease must remain valid until the five-day boundary");
    Require(DnfValidateLicenseLease(lease, lease.licenseKey, lease.machineId,
        lease.cardDuration, deadline) == DnfLicenseLeaseValidation::validationDue,
        "lease must require cloud validation at the five-day boundary");
}

void TestActualExpiryWins()
{
    auto lease = MakeLease();
    lease.expireTime = lease.validatedAt + 60;
    Require(DnfGetLicenseLeaseValidUntil(lease) == lease.expireTime,
        "card expiry must be earlier than the five-day lease deadline");
    Require(DnfValidateLicenseLease(lease, lease.licenseKey, lease.machineId,
        lease.cardDuration, lease.expireTime) == DnfLicenseLeaseValidation::cardExpired,
        "expired card must not be revived by a cached lease");

    lease = MakeLease();
    lease.cardDuration = DNF_LICENSE_PERMANENT_EXPIRY;
    lease.expireTime = DNF_LICENSE_PERMANENT_EXPIRY;
    Require(DnfGetLicenseLeaseValidUntil(lease) ==
        lease.validatedAt + DNF_LICENSE_LEASE_SECONDS,
        "permanent card must still refresh its lease every five days");
}

void TestIdentityAndClockChecks()
{
    auto lease = MakeLease();
    Require(DnfValidateLicenseLease(lease, L"different", lease.machineId,
        lease.cardDuration, lease.validatedAt + 1) ==
        DnfLicenseLeaseValidation::identityMismatch,
        "lease must be tied to the exact activation key");
    Require(DnfValidateLicenseLease(lease, lease.licenseKey, L"other-machine",
        lease.cardDuration, lease.validatedAt + 1) ==
        DnfLicenseLeaseValidation::identityMismatch,
        "lease must be tied to the exact machine id");

    lease.lastUsedAt = lease.validatedAt + 1000;
    Require(DnfValidateLicenseLease(lease, lease.licenseKey, lease.machineId,
        lease.cardDuration, lease.lastUsedAt - DNF_LICENSE_CLOCK_SKEW_SECONDS - 1) ==
        DnfLicenseLeaseValidation::clockRollback,
        "clock rollback beyond the allowance must invalidate the lease");
}

void TestMalformedLease()
{
    auto lease = MakeLease();
    lease.cloudServerUrl.clear();
    Require(DnfValidateLicenseLease(lease, lease.licenseKey, lease.machineId,
        lease.cardDuration, lease.validatedAt + 1) ==
        DnfLicenseLeaseValidation::invalidRecord,
        "lease without an authorized endpoint must be rejected");
}

void TestEndpointRefreshPolicy()
{
    constexpr std::uint64_t startedAt = 10'000;
    Require(!DnfShouldRefreshLicenseEndpoint(false, false, false, false,
        startedAt, startedAt + DNF_LICENSE_ENDPOINT_REFRESH_DELAY_MS),
        "non-lease sessions must not refresh through the lease fallback");
    Require(!DnfShouldRefreshLicenseEndpoint(true, false, false, true,
        startedAt, startedAt + DNF_LICENSE_ENDPOINT_REFRESH_DELAY_MS),
        "a connected lease endpoint must not be refreshed");
    Require(!DnfShouldRefreshLicenseEndpoint(true, false, false, false,
        startedAt, startedAt + DNF_LICENSE_ENDPOINT_REFRESH_DELAY_MS - 1),
        "a transient disconnect must not refresh before the delay");
    Require(DnfShouldRefreshLicenseEndpoint(true, false, false, false,
        startedAt, startedAt + DNF_LICENSE_ENDPOINT_REFRESH_DELAY_MS),
        "a lease endpoint disconnected for the full delay must refresh");
    Require(!DnfShouldRefreshLicenseEndpoint(true, true, false, false,
        startedAt, startedAt + DNF_LICENSE_ENDPOINT_REFRESH_DELAY_MS),
        "a refresh may run only once per process");
    Require(!DnfShouldRefreshLicenseEndpoint(true, false, true, false,
        startedAt, startedAt + DNF_LICENSE_ENDPOINT_REFRESH_DELAY_MS),
        "an in-flight refresh must not be duplicated");
    Require(!DnfShouldRefreshLicenseEndpoint(true, false, false, false,
        0, startedAt + DNF_LICENSE_ENDPOINT_REFRESH_DELAY_MS),
        "the disconnect timer must be initialized before refreshing");
    Require(!DnfShouldRefreshLicenseEndpoint(true, false, false, false,
        startedAt, startedAt - 1),
        "a monotonic tick rollback must not trigger a refresh");
}

} // namespace

int main()
{
    TestEligibilityBoundary();
    TestFiveDayWindow();
    TestActualExpiryWins();
    TestIdentityAndClockChecks();
    TestMalformedLease();
    TestEndpointRefreshPolicy();
    std::cout << "License lease policy tests passed.\n";
    return 0;
}
