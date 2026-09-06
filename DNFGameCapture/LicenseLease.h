#pragma once

#include <cstdint>
#include <string>

constexpr std::int64_t DNF_LICENSE_MONTH_SECONDS = 30LL * 24 * 60 * 60;
constexpr std::int64_t DNF_LICENSE_LEASE_SECONDS = 5LL * 24 * 60 * 60;
constexpr std::int64_t DNF_LICENSE_CLOCK_SKEW_SECONDS = 5LL * 60;
constexpr std::int64_t DNF_LICENSE_PERMANENT_EXPIRY = 0xFFFFFFFFLL;
constexpr std::uint64_t DNF_LICENSE_ENDPOINT_REFRESH_DELAY_MS = 30ULL * 1000;

struct DnfLicenseLeaseRecord {
    std::wstring licenseKey;
    std::wstring machineId;
    std::wstring cloudServerUrl;
    // Only populated by the opt-in server-auth-v2 path.  It is stored inside
    // the same DPAPI-protected record as the endpoint, never in config.ini.
    std::wstring serverSessionToken;
    std::int64_t cardDuration = 0;
    std::int64_t expireTime = 0;
    std::int64_t validatedAt = 0;
    std::int64_t lastUsedAt = 0;
};

enum class DnfLicenseLeaseValidation {
    valid,
    ineligible,
    identityMismatch,
    invalidRecord,
    clockRollback,
    cardExpired,
    validationDue
};

bool DnfIsLicenseLeaseEligible(std::int64_t cardDuration) noexcept;
std::int64_t DnfGetLicenseLeaseValidUntil(
    const DnfLicenseLeaseRecord& lease) noexcept;
DnfLicenseLeaseValidation DnfValidateLicenseLease(
    const DnfLicenseLeaseRecord& lease,
    const std::wstring& expectedLicenseKey,
    const std::wstring& expectedMachineId,
    std::int64_t expectedCardDuration,
    std::int64_t now) noexcept;
const wchar_t* DnfLicenseLeaseValidationText(
    DnfLicenseLeaseValidation result) noexcept;
bool DnfShouldRefreshLicenseEndpoint(bool usingLicenseLease,
    bool refreshAttempted, bool refreshInFlight, bool connected,
    std::uint64_t disconnectedSinceTick, std::uint64_t nowTick) noexcept;

bool DnfLoadProtectedLicenseLease(DnfLicenseLeaseRecord& lease) noexcept;
bool DnfSaveProtectedLicenseLease(
    const DnfLicenseLeaseRecord& lease) noexcept;
void DnfClearProtectedLicenseLease() noexcept;
