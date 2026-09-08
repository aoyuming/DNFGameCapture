#pragma once

#include "json.hpp"
#include <string>

namespace dnf::player_library_sync {
enum class SubmissionStatus { Failed, PendingReview, Accepted, NoChanges, AlreadyPending };

// Order and duplicates are ignored; entity IDs and name/ID ownership are retained.
nlohmann::json CanonicalPayload(const nlohmann::json& payload);
std::string SubmissionScope(const std::string& endpoint,
    const std::string& deviceId, const std::string& licenseKey);
std::string SubmissionSignature(const nlohmann::json& payload,
    const std::string& endpoint, const std::string& deviceId, const std::string& licenseKey);
SubmissionStatus ParseSubmissionStatus(unsigned int httpStatus, const nlohmann::json& reply);
bool IsAcknowledged(SubmissionStatus status) noexcept;
bool IsSkipped(SubmissionStatus status) noexcept;

class SubmissionTracker {
public:
    void Restore(const std::string& saved);
    void Clear() { acknowledged_.clear(); }
    bool ShouldSkip(const std::string& signature) const noexcept;
    bool Acknowledge(const std::string& submittedSignature, SubmissionStatus status);
    const std::string& SavedSignature() const noexcept { return acknowledged_; }
private:
    std::string acknowledged_;
};
} // namespace dnf::player_library_sync
