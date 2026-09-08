#include "PlayerLibraryPushPolicy.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

#pragma comment(lib, "bcrypt.lib")

namespace dnf::player_library_sync {
using json = nlohmann::json;
namespace {
constexpr char SignaturePrefix[] = "v2-library-sha256-v1:";
bool ValidSignature(const std::string& signature) noexcept {
    constexpr auto prefixLength = sizeof(SignaturePrefix) - 1;
    return signature.size() == prefixLength + 64 && signature.compare(0, prefixLength, SignaturePrefix) == 0 &&
        std::all_of(signature.begin() + prefixLength, signature.end(), [](char ch) {
            return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
        });
}
std::string Sha256(const std::string& value) {
    if (value.size() > (std::numeric_limits<ULONG>::max)()) return {};
    struct Handles {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        ~Handles() {
            if (hash) BCryptDestroyHash(hash);
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } handles;
    std::array<unsigned char, 32> bytes{};
    if (BCryptOpenAlgorithmProvider(&handles.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(handles.algorithm, &handles.hash, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(handles.hash, reinterpret_cast<PUCHAR>(const_cast<char*>(value.data())), static_cast<ULONG>(value.size()), 0) < 0 ||
        BCryptFinishHash(handles.hash, bytes.data(), static_cast<ULONG>(bytes.size()), 0) < 0) return {};
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (auto byte : bytes) { result.push_back(hex[byte >> 4]); result.push_back(hex[byte & 15]); }
    return result;
}
json SortedStrings(const json& values) {
    if (!values.is_array()) throw std::invalid_argument("Expected string array");
    for (const auto& value : values) if (!value.is_string() || value.get_ref<const std::string&>().empty())
        throw std::invalid_argument("Expected nonempty string");
    auto result = values;
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
}
json CanonicalPayload(const json& payload) {
    if (!payload.is_object() || !payload.contains("entities") || !payload["entities"].is_array())
        throw std::invalid_argument("Expected player entities");
    auto entities = json::array();
    for (const auto& row : payload["entities"]) {
        if (!row.is_object() || !row.contains("names")) throw std::invalid_argument("Expected player names");
        auto names = SortedStrings(row["names"]);
        if (names.empty()) throw std::invalid_argument("Player entity has no names");
        json entity = {{"names", std::move(names)}, {"gameIds", SortedStrings(row.value("gameIds", json::array()))}};
        if (row.contains("entityId") && !row["entityId"].is_null()) {
            if (!row["entityId"].is_string() || row["entityId"].get_ref<const std::string&>().empty())
                throw std::invalid_argument("Invalid cloud entity ID");
            entity["entityId"] = row["entityId"];
        }
        entities.push_back(std::move(entity));
    }
    std::sort(entities.begin(), entities.end());
    entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
    return {{"entities", std::move(entities)}};
}
std::string SubmissionScope(const std::string& endpoint,
    const std::string& deviceId, const std::string& licenseKey) {
    if (endpoint.empty() || deviceId.empty() || licenseKey.empty()) return {};
    auto normalizedEndpoint = endpoint;
    while (!normalizedEndpoint.empty() && normalizedEndpoint.back() == '/') normalizedEndpoint.pop_back();
    if (normalizedEndpoint.empty()) return {};
    return Sha256(json::array({"v2-player-library-submit-v1", normalizedEndpoint, deviceId, licenseKey}).dump());
}
std::string SubmissionSignature(const json& payload, const std::string& endpoint,
    const std::string& deviceId, const std::string& licenseKey) {
    const auto scope = SubmissionScope(endpoint, deviceId, licenseKey);
    if (scope.empty()) return {};
    const auto digest = Sha256(json::array({scope, CanonicalPayload(payload)}).dump());
    return digest.empty() ? std::string() : SignaturePrefix + digest;
}
SubmissionStatus ParseSubmissionStatus(unsigned int httpStatus, const json& reply) {
    if (httpStatus < 200 || httpStatus >= 300 || !reply.is_object() ||
        !reply.contains("ok") || !reply["ok"].is_boolean() || !reply["ok"].get<bool>()) return SubmissionStatus::Failed;
    if (!reply.contains("status")) return SubmissionStatus::Accepted;
    if (!reply["status"].is_string()) return SubmissionStatus::Failed;
    const auto state = reply["status"].get<std::string>();
    if (state == "pending_review") return SubmissionStatus::PendingReview;
    if (state == "no_changes") return SubmissionStatus::NoChanges;
    if (state == "already_pending") return SubmissionStatus::AlreadyPending;
    return SubmissionStatus::Accepted;
}
bool IsAcknowledged(SubmissionStatus status) noexcept { return status != SubmissionStatus::Failed; }
bool IsSkipped(SubmissionStatus status) noexcept {
    return status == SubmissionStatus::NoChanges || status == SubmissionStatus::AlreadyPending;
}
void SubmissionTracker::Restore(const std::string& saved) { acknowledged_ = ValidSignature(saved) ? saved : std::string(); }
bool SubmissionTracker::ShouldSkip(const std::string& signature) const noexcept {
    return ValidSignature(signature) && signature == acknowledged_;
}
bool SubmissionTracker::Acknowledge(const std::string& signature, SubmissionStatus status) {
    if (!IsAcknowledged(status) || !ValidSignature(signature) || signature == acknowledged_) return false;
    acknowledged_ = signature;
    return true;
}
} // namespace dnf::player_library_sync
