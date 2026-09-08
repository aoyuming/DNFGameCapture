#include "../CloudReleasePolicy.h"
#include "../LicenseLease.h"

#include <windows.h>
#include <wincrypt.h>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
namespace policy = dnf::cloud_release;
using nlohmann::json;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::string ExpectedEnvironment()
{
#if defined(DNF_CLOUD_TEST_BUILD) && DNF_CLOUD_TEST_BUILD == 1
    return "test";
#else
    return "production";
#endif
}

json Manifest()
{
    return { { "environment", ExpectedEnvironment() }, { "protocolVersion", 2 },
        { "cloudServerUrl", "http://203.0.113.7:18880" } };
}

DnfLicenseLeaseRecord PermanentLease()
{
    DnfLicenseLeaseRecord lease;
    lease.licenseKey = L"fixture-permanent-card";
    lease.machineId = L"fixture-machine";
    lease.cloudServerUrl = L"http://203.0.113.7:18880";
    lease.serverSessionToken = L"fixture-v2-session";
    lease.cardDuration = lease.expireTime = DNF_LICENSE_PERMANENT_EXPIRY;
    lease.validatedAt = lease.lastUsedAt = 1'700'000'000;
    return lease;
}

void TestCompiledEnvironmentAndCache()
{
    const auto expected = ExpectedEnvironment();
    Require(policy::CurrentEnvironment() == std::wstring(expected.begin(), expected.end()),
        "compiled environment must default to production, test only with macro=1");
    const std::wstring manifest = expected == "test"
        ? L"https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/cloud-server-test.json"
        : L"https://dnf-capture-update.oss-cn-beijing.aliyuncs.com/cloud-server-prod.json";
    Require(policy::CurrentManifestUrl() == manifest, "manifest must be the canonical OSS URL");
    Require(!policy::CanUseCachedEndpoint(L"", L"", L"http://203.0.113.7:18880"),
        "legacy unmarked cache must force the first manifest fetch");
    Require(!policy::CanUseCachedEndpoint(L"", manifest, L"http://203.0.113.7:18880"),
        "a manifest URL alone cannot silently assign an environment to old cache");
    Require(!policy::CanUseCachedEndpoint(policy::CurrentEnvironment(), L"", L"http://203.0.113.7:18880"),
        "cache must carry the active manifest marker");
    Require(!policy::CanUseCachedEndpoint(expected == "test" ? L"production" : L"test", manifest,
        L"http://203.0.113.7:18880"), "cross-environment cache must be rejected");
    Require(!policy::CanUseCachedEndpoint(policy::CurrentEnvironment(), manifest + L"?old=1",
        L"http://203.0.113.7:18880"), "noncanonical manifest must be rejected");
    for (const auto* endpoint : { L"http://203.0.113.7:18880", L"https://new.example.net:443/cloud",
        L"http://198.51.100.23:28880/", L"https://[2001:db8::1]:8443" }) {
        Require(policy::CanUseCachedEndpoint(policy::CurrentEnvironment(), manifest, endpoint),
            "matching cache may use moved IPs, DNS hosts, or IPv6 without IP pinning");
    }
}

void TestManifestValidation()
{
    std::wstring endpoint, error;
    auto manifest = Manifest();
    Require(policy::ValidateManifest(manifest, endpoint, error), "active V2 manifest must validate");
    Require(endpoint == L"http://203.0.113.7:18880" && error.empty(), "endpoint and error outputs");
    for (const auto& bad : { json(), json::array(), json::object() }) {
        endpoint = L"old";
        Require(!policy::ValidateManifest(bad, endpoint, error) && endpoint.empty() && !error.empty(),
            "bad manifest must fail closed and clear previous endpoint");
    }
    for (const auto& environment : { json(""), json("PRODUCTION"), json(2), json(nullptr),
        json(ExpectedEnvironment() == "test" ? "production" : "test") }) {
        manifest = Manifest(); manifest["environment"] = environment;
        Require(!policy::ValidateManifest(manifest, endpoint, error), "environment must exactly match release");
    }
    for (const auto& version : { json(1), json(3), json("2"), json(2.0), json(true), json(nullptr),
        json(18446744073709551615ULL) }) {
        manifest = Manifest(); manifest["protocolVersion"] = version;
        Require(!policy::ValidateManifest(manifest, endpoint, error), "only integer protocol version 2 is valid");
    }
    for (const auto& invalid : { std::string(), std::string("ftp://example.com"), std::string("https:///path"),
        std::string("http://user:secret@example.com"), std::string("http://example.com?token=x"),
        std::string("https://example.com/#fragment"), std::string(" https://example.com"),
        std::string("https://exa mple.com"), std::string("https://example.com\\wrong"),
        std::string("https://example.com:65536"), std::string("https://example.com:0"),
        std::string("https://example.com:"), std::string("https://example.com:bad"),
        std::string("https://example.com\r\nInjected: header"), std::string("https://example.com\0evil", 24),
        std::string("https://example.com/\xff"), std::string(2049, 'x') }) {
        manifest = Manifest(); manifest["cloudServerUrl"] = invalid;
        Require(!policy::ValidateManifest(manifest, endpoint, error) && endpoint.empty(),
            "unsafe or malformed endpoint must be rejected");
    }
    manifest = Manifest(); manifest.erase("protocolVersion");
    Require(!policy::ValidateManifest(manifest, endpoint, error), "missing protocol must not default to V2");
    manifest = Manifest(); manifest["cloudServerUrl"] = 42;
    Require(!policy::ValidateManifest(manifest, endpoint, error), "endpoint must be a string");
    Require(!policy::CanUseCachedEndpoint(policy::CurrentEnvironment(), policy::CurrentManifestUrl(),
        L"https://example.com?unsafe=1"), "cached endpoint gets the same URL validation");
}

class RegistryFixture {
public:
    RegistryFixture()
    {
        path_ = L"Software\\DNFCapturePolicyTest-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64());
        Require(RegCreateKeyExW(HKEY_CURRENT_USER, path_.c_str(), 0, nullptr, 0,
            KEY_ALL_ACCESS, nullptr, &root_, nullptr) == ERROR_SUCCESS, "create isolated registry root");
        Require(RegOverridePredefKey(HKEY_CURRENT_USER, root_) == ERROR_SUCCESS, "isolate lease registry IO");
    }
    ~RegistryFixture()
    {
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        RegCloseKey(root_);
        RegDeleteTreeW(HKEY_CURRENT_USER, path_.c_str());
    }
    void WriteEncrypted(const std::vector<BYTE>& plain)
    {
        DATA_BLOB input{ static_cast<DWORD>(plain.size()), const_cast<BYTE*>(plain.data()) }, output{};
        Require(CryptProtectData(&input, L"Legacy fixture", nullptr, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &output) != FALSE, "protect legacy fixture with real DPAPI");
        HKEY key = nullptr;
        const auto opened = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\DNFCapture", 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &key, nullptr);
        const auto written = opened == ERROR_SUCCESS ? RegSetValueExW(key, L"LicenseLeaseV1", 0,
            REG_BINARY, output.pbData, output.cbData) : opened;
        if (key) RegCloseKey(key);
        SecureZeroMemory(output.pbData, output.cbData); LocalFree(output.pbData);
        Require(written == ERROR_SUCCESS, "write protected fixture in isolated registry");
    }
private:
    HKEY root_ = nullptr;
    std::wstring path_;
};

std::vector<BYTE> LegacyBytes(const DnfLicenseLeaseRecord& lease, unsigned version)
{
    std::vector<BYTE> bytes;
    const auto number = [&](std::uint64_t value, int length) {
        for (int i = 0; i < length; ++i) bytes.push_back(static_cast<BYTE>((value >> (8 * i)) & 0xff));
    };
    const auto text = [&](const std::wstring& value) {
        number(value.size(), 4);
        for (const auto ch : value) bytes.push_back(static_cast<BYTE>(ch));
    };
    number(0x314C4644, 4); number(version, 4);
    number(lease.cardDuration, 8); number(lease.expireTime, 8);
    number(lease.validatedAt, 8); number(lease.lastUsedAt, 8);
    text(lease.licenseKey); text(lease.machineId); text(lease.cloudServerUrl);
    if (version >= 2) text(lease.serverSessionToken);
    return bytes;
}

void TestProtectedLeaseCompatibility()
{
    RegistryFixture registry;
    const auto old = PermanentLease();
    for (const unsigned version : { 1U, 2U }) {
        registry.WriteEncrypted(LegacyBytes(old, version));
        DnfLicenseLeaseRecord loaded;
        Require(DnfLoadProtectedLicenseLease(loaded), "decrypt and read legacy permanent lease");
        Require(loaded.licenseKey == old.licenseKey && loaded.machineId == old.machineId &&
            loaded.expireTime == DNF_LICENSE_PERMANENT_EXPIRY, "old CDK and permanent eligibility remain unchanged");
        Require(loaded.serverSessionToken == (version == 2 ? old.serverSessionToken : L""), "legacy V2 session compatibility");
        Require(loaded.environment.empty() && loaded.endpointManifestUrl.empty(), "legacy scope stays absent");
        Require(DnfValidateLicenseLease(loaded, old.licenseKey, old.machineId, old.cardDuration,
            old.validatedAt + 1) == DnfLicenseLeaseValidation::valid, "generic old lease validator stays compatible");
        Require(!policy::LeaseMatchesRelease(loaded), "unscoped permanent lease must renew online for current release");
        Require(DnfSaveProtectedLicenseLease(loaded) && DnfLoadProtectedLicenseLease(loaded), "old permanent lease crypto roundtrip");
        Require(!policy::LeaseMatchesRelease(loaded), "resaving legacy lease must not silently assign scope");
    }
    auto scoped = old;
    scoped.environment = policy::CurrentEnvironment(); scoped.endpointManifestUrl = policy::CurrentManifestUrl();
    Require(DnfSaveProtectedLicenseLease(scoped), "save scoped lease through DPAPI");
    DnfLicenseLeaseRecord loaded;
    Require(DnfLoadProtectedLicenseLease(loaded), "load scoped lease through DPAPI");
    Require(loaded.environment == scoped.environment && loaded.endpointManifestUrl == scoped.endpointManifestUrl &&
        loaded.serverSessionToken == scoped.serverSessionToken, "scope and session survive encrypted roundtrip");
    Require(policy::LeaseMatchesRelease(loaded), "matching release scope can be used alongside generic validation");
    loaded.environment = ExpectedEnvironment() == "test" ? L"production" : L"test";
    Require(!policy::LeaseMatchesRelease(loaded), "cross-environment lease must not authorize");
    loaded = scoped; loaded.endpointManifestUrl += L"?old=1";
    Require(!policy::LeaseMatchesRelease(loaded), "wrong manifest context must not authorize");
    loaded = scoped; loaded.cloudServerUrl = L"file:///endpoint";
    Require(!policy::LeaseMatchesRelease(loaded), "scoped lease with invalid endpoint must not authorize");
    auto truncated = LegacyBytes(old, 3); registry.WriteEncrypted(truncated);
    Require(!DnfLoadProtectedLicenseLease(loaded) && loaded.licenseKey.empty(), "truncated scope must fail closed");
    DnfClearProtectedLicenseLease();
    Require(!DnfLoadProtectedLicenseLease(loaded), "clear only isolated test lease");
}
} // namespace

int main()
{
    try {
        TestCompiledEnvironmentAndCache(); TestManifestValidation(); TestProtectedLeaseCompatibility();
        std::cout << "Cloud release policy tests passed (" << ExpectedEnvironment() << ").\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
