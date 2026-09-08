#pragma once

#include "json.hpp"
#include <string>

struct DnfLicenseLeaseRecord;

namespace dnf::cloud_release {

// Immutable for the release. Only DNF_CLOUD_TEST_BUILD=1 selects test.
const wchar_t* CurrentEnvironment() noexcept;
const wchar_t* CurrentManifestUrl() noexcept;

// Missing legacy markers require a manifest fetch, not a guessed environment.
bool CanUseCachedEndpoint(const std::wstring& storedEnv,
    const std::wstring& storedManifest, const std::wstring& endpoint) noexcept;

// Scope check only: callers must also use DnfValidateLicenseLease and check V2 auth.
bool LeaseMatchesRelease(const DnfLicenseLeaseRecord& lease) noexcept;

// Requires exact environment, integer protocolVersion=2 and an HTTP(S) base URL.
// No network IO or IP pinning. Failure clears outEndpoint and supplies an error.
bool ValidateManifest(const nlohmann::json& manifest,
    std::wstring& outEndpoint, std::wstring& error);

} // namespace dnf::cloud_release
