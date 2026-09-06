#include "LicenseLease.h"

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Advapi32.lib")

namespace {

constexpr wchar_t DNF_LICENSE_LEASE_REG_PATH[] = L"Software\\DNFCapture";
constexpr wchar_t DNF_LICENSE_LEASE_REG_VALUE[] = L"LicenseLeaseV1";
constexpr std::uint32_t DNF_LICENSE_LEASE_MAGIC = 0x314C4644; // DFL1
constexpr std::uint32_t DNF_LICENSE_LEASE_VERSION = 2;
constexpr DWORD DNF_LICENSE_LEASE_MAX_BYTES = 64 * 1024;
constexpr std::size_t DNF_LICENSE_LEASE_MAX_TEXT_BYTES = 16 * 1024;

void SecureWipe(std::vector<BYTE>& value) noexcept
{
    if (!value.empty()) {
        ::SecureZeroMemory(value.data(), value.size());
        value.clear();
    }
}

bool WideToUtf8(const std::wstring& input, std::vector<BYTE>& output)
{
    output.clear();
    if (input.empty()) return true;
    if (input.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    const int needed = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0 || static_cast<std::size_t>(needed) >
        DNF_LICENSE_LEASE_MAX_TEXT_BYTES) {
        return false;
    }
    output.resize(static_cast<std::size_t>(needed));
    return ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        input.data(), static_cast<int>(input.size()),
        reinterpret_cast<char*>(output.data()), needed, nullptr, nullptr) == needed;
}

bool Utf8ToWide(const BYTE* bytes, std::size_t length, std::wstring& output)
{
    output.clear();
    if (length == 0) return true;
    if (!bytes || length > DNF_LICENSE_LEASE_MAX_TEXT_BYTES ||
        length > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        return false;
    }
    const int needed = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        reinterpret_cast<const char*>(bytes), static_cast<int>(length), nullptr, 0);
    if (needed <= 0) return false;
    output.resize(static_cast<std::size_t>(needed));
    return ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        reinterpret_cast<const char*>(bytes), static_cast<int>(length),
        output.data(), needed) == needed;
}

void AppendU32(std::vector<BYTE>& output, std::uint32_t value)
{
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<BYTE>((value >> shift) & 0xFF));
    }
}

void AppendI64(std::vector<BYTE>& output, std::int64_t value)
{
    const std::uint64_t bits = static_cast<std::uint64_t>(value);
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        output.push_back(static_cast<BYTE>((bits >> shift) & 0xFF));
    }
}

bool ReadU32(const BYTE*& cursor, const BYTE* end, std::uint32_t& value)
{
    if (!cursor || !end || end - cursor < 4) return false;
    value = 0;
    for (unsigned int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(*cursor++) << shift;
    }
    return true;
}

bool ReadI64(const BYTE*& cursor, const BYTE* end, std::int64_t& value)
{
    if (!cursor || !end || end - cursor < 8) return false;
    std::uint64_t bits = 0;
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        bits |= static_cast<std::uint64_t>(*cursor++) << shift;
    }
    value = static_cast<std::int64_t>(bits);
    return true;
}

bool AppendText(std::vector<BYTE>& output, const std::wstring& text)
{
    std::vector<BYTE> utf8;
    if (!WideToUtf8(text, utf8) ||
        utf8.size() > (std::numeric_limits<std::uint32_t>::max)()) {
        SecureWipe(utf8);
        return false;
    }
    AppendU32(output, static_cast<std::uint32_t>(utf8.size()));
    output.insert(output.end(), utf8.begin(), utf8.end());
    SecureWipe(utf8);
    return true;
}

bool ReadText(const BYTE*& cursor, const BYTE* end, std::wstring& text)
{
    std::uint32_t length = 0;
    if (!ReadU32(cursor, end, length) ||
        length > DNF_LICENSE_LEASE_MAX_TEXT_BYTES ||
        static_cast<std::size_t>(end - cursor) < length) {
        return false;
    }
    const bool converted = Utf8ToWide(cursor, length, text);
    cursor += length;
    return converted;
}

bool SerializeLease(const DnfLicenseLeaseRecord& lease,
    std::vector<BYTE>& output)
{
    output.clear();
    output.reserve(512);
    AppendU32(output, DNF_LICENSE_LEASE_MAGIC);
    AppendU32(output, DNF_LICENSE_LEASE_VERSION);
    AppendI64(output, lease.cardDuration);
    AppendI64(output, lease.expireTime);
    AppendI64(output, lease.validatedAt);
    AppendI64(output, lease.lastUsedAt);
    if (!AppendText(output, lease.licenseKey) ||
        !AppendText(output, lease.machineId) ||
        !AppendText(output, lease.cloudServerUrl) ||
        !AppendText(output, lease.serverSessionToken) ||
        output.size() > DNF_LICENSE_LEASE_MAX_BYTES) {
        SecureWipe(output);
        return false;
    }
    return true;
}

bool DeserializeLease(const BYTE* bytes, std::size_t length,
    DnfLicenseLeaseRecord& lease)
{
    lease = {};
    if (!bytes || length == 0 || length > DNF_LICENSE_LEASE_MAX_BYTES) return false;
    const BYTE* cursor = bytes;
    const BYTE* end = bytes + length;
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    if (!ReadU32(cursor, end, magic) || !ReadU32(cursor, end, version) ||
        magic != DNF_LICENSE_LEASE_MAGIC || (version != 1 && version != 2) ||
        !ReadI64(cursor, end, lease.cardDuration) ||
        !ReadI64(cursor, end, lease.expireTime) ||
        !ReadI64(cursor, end, lease.validatedAt) ||
        !ReadI64(cursor, end, lease.lastUsedAt) ||
        !ReadText(cursor, end, lease.licenseKey) ||
        !ReadText(cursor, end, lease.machineId) ||
        !ReadText(cursor, end, lease.cloudServerUrl) ||
        (version == 2 && !ReadText(cursor, end, lease.serverSessionToken)) ||
        cursor != end) {
        lease = {};
        return false;
    }
    return true;
}

} // namespace

bool DnfIsLicenseLeaseEligible(std::int64_t cardDuration) noexcept
{
    return cardDuration == DNF_LICENSE_PERMANENT_EXPIRY ||
        cardDuration >= DNF_LICENSE_MONTH_SECONDS;
}

std::int64_t DnfGetLicenseLeaseValidUntil(
    const DnfLicenseLeaseRecord& lease) noexcept
{
    if (lease.validatedAt <= 0 ||
        lease.validatedAt > (std::numeric_limits<std::int64_t>::max)() -
            DNF_LICENSE_LEASE_SECONDS) {
        return 0;
    }
    const std::int64_t leaseDeadline =
        lease.validatedAt + DNF_LICENSE_LEASE_SECONDS;
    if (lease.expireTime == DNF_LICENSE_PERMANENT_EXPIRY) {
        return leaseDeadline;
    }
    if (lease.expireTime <= 0) return 0;
    return (std::min)(leaseDeadline, lease.expireTime);
}

DnfLicenseLeaseValidation DnfValidateLicenseLease(
    const DnfLicenseLeaseRecord& lease,
    const std::wstring& expectedLicenseKey,
    const std::wstring& expectedMachineId,
    std::int64_t expectedCardDuration,
    std::int64_t now) noexcept
{
    if (!DnfIsLicenseLeaseEligible(expectedCardDuration) ||
        !DnfIsLicenseLeaseEligible(lease.cardDuration)) {
        return DnfLicenseLeaseValidation::ineligible;
    }
    if (lease.licenseKey != expectedLicenseKey ||
        lease.machineId != expectedMachineId ||
        lease.cardDuration != expectedCardDuration) {
        return DnfLicenseLeaseValidation::identityMismatch;
    }
    if (lease.licenseKey.empty() || lease.machineId.empty() ||
        lease.cloudServerUrl.empty() || lease.cloudServerUrl.size() > 2048 ||
        lease.cloudServerUrl.find(L'\0') != std::wstring::npos ||
        lease.validatedAt <= 0 || lease.lastUsedAt < lease.validatedAt || now <= 0 ||
        DnfGetLicenseLeaseValidUntil(lease) <= 0) {
        return DnfLicenseLeaseValidation::invalidRecord;
    }
    if (now + DNF_LICENSE_CLOCK_SKEW_SECONDS < lease.validatedAt ||
        now + DNF_LICENSE_CLOCK_SKEW_SECONDS < lease.lastUsedAt) {
        return DnfLicenseLeaseValidation::clockRollback;
    }
    if (lease.expireTime != DNF_LICENSE_PERMANENT_EXPIRY &&
        now >= lease.expireTime) {
        return DnfLicenseLeaseValidation::cardExpired;
    }
    if (now >= DnfGetLicenseLeaseValidUntil(lease)) {
        return DnfLicenseLeaseValidation::validationDue;
    }
    return DnfLicenseLeaseValidation::valid;
}

const wchar_t* DnfLicenseLeaseValidationText(
    DnfLicenseLeaseValidation result) noexcept
{
    switch (result) {
    case DnfLicenseLeaseValidation::valid: return L"租约有效";
    case DnfLicenseLeaseValidation::ineligible: return L"卡片时长不足一个月";
    case DnfLicenseLeaseValidation::identityMismatch: return L"卡密或机器码不匹配";
    case DnfLicenseLeaseValidation::invalidRecord: return L"租约内容无效";
    case DnfLicenseLeaseValidation::clockRollback: return L"检测到系统时间回拨";
    case DnfLicenseLeaseValidation::cardExpired: return L"卡片已到期";
    case DnfLicenseLeaseValidation::validationDue: return L"已到五天联网验证时间";
    default: return L"未知租约状态";
    }
}

bool DnfShouldRefreshLicenseEndpoint(bool usingLicenseLease,
    bool refreshAttempted, bool refreshInFlight, bool connected,
    std::uint64_t disconnectedSinceTick, std::uint64_t nowTick) noexcept
{
    if (!usingLicenseLease || refreshAttempted || refreshInFlight || connected ||
        disconnectedSinceTick == 0 || nowTick < disconnectedSinceTick) {
        return false;
    }
    return nowTick - disconnectedSinceTick >=
        DNF_LICENSE_ENDPOINT_REFRESH_DELAY_MS;
}

bool DnfLoadProtectedLicenseLease(DnfLicenseLeaseRecord& lease) noexcept
{
    lease = {};
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, DNF_LICENSE_LEASE_REG_PATH, 0,
        KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD bytes = 0;
    LONG rc = ::RegQueryValueExW(key, DNF_LICENSE_LEASE_REG_VALUE, nullptr,
        &type, nullptr, &bytes);
    if (rc != ERROR_SUCCESS || type != REG_BINARY || bytes == 0 ||
        bytes > DNF_LICENSE_LEASE_MAX_BYTES) {
        ::RegCloseKey(key);
        return false;
    }

    std::vector<BYTE> encrypted(bytes);
    rc = ::RegQueryValueExW(key, DNF_LICENSE_LEASE_REG_VALUE, nullptr,
        &type, encrypted.data(), &bytes);
    ::RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_BINARY) {
        SecureWipe(encrypted);
        return false;
    }

    DATA_BLOB input{};
    input.cbData = bytes;
    input.pbData = encrypted.data();
    DATA_BLOB output{};
    const BOOL decrypted = ::CryptUnprotectData(&input, nullptr, nullptr,
        nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    SecureWipe(encrypted);
    if (!decrypted || !output.pbData || output.cbData == 0 ||
        output.cbData > DNF_LICENSE_LEASE_MAX_BYTES) {
        if (output.pbData) ::LocalFree(output.pbData);
        return false;
    }

    const bool parsed = DeserializeLease(output.pbData, output.cbData, lease);
    ::SecureZeroMemory(output.pbData, output.cbData);
    ::LocalFree(output.pbData);
    return parsed;
}

bool DnfSaveProtectedLicenseLease(
    const DnfLicenseLeaseRecord& lease) noexcept
{
    std::vector<BYTE> plain;
    if (!SerializeLease(lease, plain)) return false;

    DATA_BLOB input{};
    input.cbData = static_cast<DWORD>(plain.size());
    input.pbData = plain.data();
    DATA_BLOB output{};
    const BOOL encrypted = ::CryptProtectData(&input,
        L"DNF Capture authorization lease", nullptr, nullptr, nullptr,
        CRYPTPROTECT_UI_FORBIDDEN, &output);
    SecureWipe(plain);
    if (!encrypted || !output.pbData || output.cbData == 0 ||
        output.cbData > DNF_LICENSE_LEASE_MAX_BYTES) {
        if (output.pbData) ::LocalFree(output.pbData);
        return false;
    }

    HKEY key = nullptr;
    DWORD disposition = 0;
    const LONG createResult = ::RegCreateKeyExW(HKEY_CURRENT_USER,
        DNF_LICENSE_LEASE_REG_PATH, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
        &key, &disposition);
    bool saved = false;
    if (createResult == ERROR_SUCCESS) {
        saved = ::RegSetValueExW(key, DNF_LICENSE_LEASE_REG_VALUE, 0,
            REG_BINARY, output.pbData, output.cbData) == ERROR_SUCCESS;
        ::RegCloseKey(key);
    }
    ::SecureZeroMemory(output.pbData, output.cbData);
    ::LocalFree(output.pbData);
    return saved;
}

void DnfClearProtectedLicenseLease() noexcept
{
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, DNF_LICENSE_LEASE_REG_PATH, 0,
        KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        ::RegDeleteValueW(key, DNF_LICENSE_LEASE_REG_VALUE);
        ::RegCloseKey(key);
    }
}
