#include "PlayerLibraryPushPolicy.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace dnf::player_library_sync;
using json = nlohmann::json;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Check failed: " #x); } while (false)
template<class F> void Reject(F action) { bool failed = false; try { action(); } catch (...) { failed = true; } CHECK(failed); }

json Payload() {
    return {{"entities", json::array({
        {{"entityId", "cloud-A"}, {"names", {"Alpha", "Alias"}}, {"gameIds", {"Role#Job", "Shared"}}},
        {{"names", {"Beta"}}, {"gameIds", {"Shared"}}},
        {{"names", {"NameOnly"}}, {"gameIds", json::array()}}
    })}};
}
std::string Signature(const json& payload) {
    return SubmissionSignature(payload, "https://prod.example/api", "machine", "private-license-key");
}
int main() {
    try {
        const auto payload = Payload();
        const auto original = Signature(payload);
        CHECK(!original.empty() && original.find("private-license-key") == std::string::npos);
        CHECK(original.find("machine") == std::string::npos && original.find("prod.example") == std::string::npos);
        auto reordered = payload;
        std::reverse(reordered["entities"].begin(), reordered["entities"].end());
        auto& first = reordered["entities"].back();
        first["names"] = {"Alias", "Alpha", "Alias"};
        first["gameIds"] = {"Shared", "Role#Job", "Shared"};
        reordered["entities"].push_back(first);
        CHECK(Signature(reordered) == original);
        CHECK(CanonicalPayload(reordered) == CanonicalPayload(payload));
        for (int change = 0; change < 6; ++change) {
            auto changed = payload;
            if (change == 0) changed["entities"][0]["entityId"] = "cloud-B";
            if (change == 1) changed["entities"][0]["gameIds"] = {"Role#Other", "Shared"};
            if (change == 2) changed["entities"][0]["names"] = {"Alpha", "Renamed"};
            if (change == 3) changed["entities"][0]["gameIds"] = {"role#Job", "Shared"};
            if (change == 4) {
                changed["entities"][0]["names"] = {"Alpha"};
                changed["entities"].push_back({{"names", {"Alias"}}, {"gameIds", {"Role#Job", "Shared"}}});
            }
            if (change == 5) changed["entities"][1]["gameIds"] = json::array();
            CHECK(Signature(changed) != original);
        }
        CHECK(SubmissionSignature(payload, "https://test.example/api", "machine", "private-license-key") != original);
        CHECK(SubmissionSignature(payload, "https://prod.example/other", "machine", "private-license-key") != original);
        CHECK(SubmissionSignature(payload, "https://prod.example/api", "other-machine", "private-license-key") != original);
        CHECK(SubmissionSignature(payload, "https://prod.example/api", "machine", "another-key") != original);
        CHECK(SubmissionSignature(payload, "https://prod.example/api/", "machine", "private-license-key") == original);
        CHECK(SubmissionSignature(payload, "", "machine", "private-license-key").empty());
        CHECK(SubmissionSignature(payload, "endpoint", "", "private-license-key").empty());
        CHECK(SubmissionSignature(payload, "endpoint", "machine", "").empty());

        SubmissionTracker manual, automatic;
        CHECK(!manual.ShouldSkip(original));
        manual.Restore("ABCDEF0123456789");
        CHECK(!manual.ShouldSkip(original) && manual.SavedSignature().empty());
        CHECK(!manual.Acknowledge(original, SubmissionStatus::Failed));
        CHECK(!manual.ShouldSkip(original));
        CHECK(manual.Acknowledge(original, SubmissionStatus::PendingReview));
        automatic.Restore(manual.SavedSignature());
        CHECK(automatic.ShouldSkip(Signature(reordered)));
        auto editedWhileInFlight = payload;
        editedWhileInFlight["entities"][0]["gameIds"].push_back("NewGame");
        const auto edited = Signature(editedWhileInFlight);
        CHECK(!automatic.ShouldSkip(edited));
        CHECK(!automatic.Acknowledge(edited, SubmissionStatus::Failed));
        CHECK(automatic.ShouldSkip(original) && !automatic.ShouldSkip(edited));
        CHECK(automatic.Acknowledge(edited, SubmissionStatus::NoChanges));
        manual.Restore(automatic.SavedSignature());
        CHECK(manual.ShouldSkip(edited) && !manual.ShouldSkip(original));
        CHECK(!manual.ShouldSkip(""));
        CHECK(!manual.Acknowledge("", SubmissionStatus::Accepted));
        manual.Clear(); CHECK(manual.SavedSignature().empty());

        for (const auto& value : {std::pair{"pending_review", SubmissionStatus::PendingReview},
            {"no_changes", SubmissionStatus::NoChanges}, {"already_pending", SubmissionStatus::AlreadyPending}}) {
            const json response = {{"ok", true}, {"status", value.first}};
            CHECK(ParseSubmissionStatus(200, response) == value.second);
            CHECK(IsAcknowledged(value.second));
            CHECK(IsSkipped(value.second) == (value.second != SubmissionStatus::PendingReview));
            CHECK(ParseSubmissionStatus(500, response) == SubmissionStatus::Failed);
            CHECK(ParseSubmissionStatus(401, response) == SubmissionStatus::Failed);
        }
        CHECK(ParseSubmissionStatus(202, {{"ok", true}, {"status", "pending_review"}}) == SubmissionStatus::PendingReview);
        CHECK(ParseSubmissionStatus(200, {{"ok", true}}) == SubmissionStatus::Accepted);
        CHECK(ParseSubmissionStatus(200, {{"ok", false}, {"status", "no_changes"}}) == SubmissionStatus::Failed);
        CHECK(ParseSubmissionStatus(200, {{"ok", "true"}, {"status", "no_changes"}}) == SubmissionStatus::Failed);
        CHECK(ParseSubmissionStatus(200, {{"ok", true}, {"status", 1}}) == SubmissionStatus::Failed);
        CHECK(ParseSubmissionStatus(200, json::array()) == SubmissionStatus::Failed);
        for (const auto& invalid : {json(), json::array(), json::object(), json{{"entities", 42}},
            json{{"entities", json::array({{{"names", "Alpha"}, {"gameIds", {"X"}}}})}},
            json{{"entities", json::array({{{"names", {"Alpha"}}, {"gameIds", {1}}}})}}}) {
            Reject([&] { CanonicalPayload(invalid); });
        }
        CHECK(CanonicalPayload(payload)["entities"].size() == 3);
        CHECK(Payload() == payload);
        std::cout << "Player library push canonicalization, scope, acknowledgments and in-flight edits passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
