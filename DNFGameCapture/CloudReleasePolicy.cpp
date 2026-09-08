#include "CloudReleasePolicy.h"
#include "LicenseLease.h"

#include <windows.h>
#include <winhttp.h>
#include <string_view>

#pragma comment(lib, "Winhttp.lib")

namespace dnf::cloud_release {
namespace {
#if defined(DNF_CLOUD_TEST_BUILD) && DNF_CLOUD_TEST_BUILD == 1
constexpr auto Environment = L"test";
constexpr auto EnvironmentUtf8 = "test";
constexpr auto ManifestUrl = L"https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/cloud-server-test.json";
#else
constexpr auto Environment = L"production";
constexpr auto EnvironmentUtf8 = "production";
constexpr auto ManifestUrl = L"https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/cloud-server-prod.json";
#endif

bool IsValidEndpoint(const std::wstring& endpoint) noexcept
{
    if (endpoint.empty() || endpoint.size() > 2048) return false;
    for (const auto ch : endpoint) {
        if (ch <= L' ' || ch == 127 || ch == L'\\' || ch == L'@' || ch == L'?' ||
            ch == L'#' || ch == L'"' || ch == L'<' || ch == L'>') return false;
    }
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUserNameLength =
        parts.dwPasswordLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(endpoint.c_str(), static_cast<DWORD>(endpoint.size()), 0, &parts) ||
        (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) ||
        !parts.dwHostNameLength || !parts.nPort || parts.dwUserNameLength ||
        parts.dwPasswordLength || parts.dwExtraInfoLength) return false;

    const auto schemeEnd = endpoint.find(L"://");
    if (schemeEnd == std::wstring::npos) return false;
    const std::wstring_view rest(endpoint.data() + schemeEnd + 3, endpoint.size() - schemeEnd - 3);
    const auto authority = rest.substr(0, rest.find(L'/'));
    if (authority.empty()) return false;
    auto hostEnd = authority.find(L':');
    if (authority.front() == L'[') {
        const auto bracket = authority.find(L']');
        if (bracket == std::wstring_view::npos) return false;
        hostEnd = bracket + 1;
        if (hostEnd == authority.size()) return true;
        if (authority[hostEnd] != L':') return false;
    }
    if (hostEnd == std::wstring_view::npos) return true;
    const auto port = authority.substr(hostEnd + 1);
    if (port.empty()) return false;
    unsigned value = 0;
    for (const auto ch : port) {
        if (ch < L'0' || ch > L'9') return false;
        value = value * 10 + static_cast<unsigned>(ch - L'0');
        if (value > 65535) return false;
    }
    return value != 0;
}

bool DecodeEndpoint(const std::string& value, std::wstring& endpoint)
{
    if (value.empty() || value.size() > 2048) return false;
    const int size = static_cast<int>(value.size());
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), size, nullptr, 0);
    if (needed <= 0) return false;
    endpoint.resize(static_cast<std::size_t>(needed));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), size, endpoint.data(), needed) == needed &&
        IsValidEndpoint(endpoint);
}
} // namespace

const wchar_t* CurrentEnvironment() noexcept { return Environment; }
const wchar_t* CurrentManifestUrl() noexcept { return ManifestUrl; }

bool CanUseCachedEndpoint(const std::wstring& storedEnv,
    const std::wstring& storedManifest, const std::wstring& endpoint) noexcept
{
    return storedEnv == Environment && storedManifest == ManifestUrl && IsValidEndpoint(endpoint);
}

bool LeaseMatchesRelease(const DnfLicenseLeaseRecord& lease) noexcept
{
    return CanUseCachedEndpoint(lease.environment, lease.endpointManifestUrl, lease.cloudServerUrl);
}

bool ValidateManifest(const nlohmann::json& manifest,
    std::wstring& outEndpoint, std::wstring& error)
{
    outEndpoint.clear(); error.clear();
    if (!manifest.is_object()) {
        error = L"Endpoint manifest must be a JSON object.";
        return false;
    }
    const auto environment = manifest.find("environment");
    if (environment == manifest.end() || !environment->is_string() ||
        environment->get_ref<const std::string&>() != EnvironmentUtf8) {
        error = L"Endpoint manifest environment does not match this release.";
        return false;
    }
    const auto protocol = manifest.find("protocolVersion");
    if (protocol == manifest.end() || !protocol->is_number_integer() || *protocol != 2) {
        error = L"Endpoint manifest must specify protocolVersion 2.";
        return false;
    }
    const auto endpoint = manifest.find("cloudServerUrl");
    if (endpoint == manifest.end() || !endpoint->is_string() ||
        !DecodeEndpoint(endpoint->get_ref<const std::string&>(), outEndpoint)) {
        outEndpoint.clear();
        error = L"Endpoint manifest cloudServerUrl is not a valid HTTP(S) base URL.";
        return false;
    }
    return true;
}

} // namespace dnf::cloud_release
