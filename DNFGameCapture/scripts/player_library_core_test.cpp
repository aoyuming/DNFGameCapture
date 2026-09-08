#include "PlayerLibraryStore.h"
#include "third_party/sqlite/sqlite3.h"
#define NOMINMAX
#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace dnf::player_library;
using json = nlohmann::json;
namespace fs = std::filesystem;
#ifdef DNF_PLAYER_LIBRARY_TESTING
namespace dnf::player_library {
void ValidateCloudNameForTesting(const std::wstring& text, bool forceFallback);
bool HasSystemIcuForTesting();
}
#endif
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Check failed: " #x); } while (false)
template<class F> void Reject(F action) { bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; } CHECK(rejected); }
void Write(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f << s; CHECK(f.good()); }
struct Connection {
    sqlite3* db = nullptr;
    explicit Connection(const fs::path& path) { CHECK(sqlite3_open(ToUtf8(path.wstring()).c_str(), &db) == SQLITE_OK); }
    ~Connection() { sqlite3_close(db); }
    void Exec(const char* sql) { CHECK(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK); }
    void SetMetadata(const json& metadata) {
        sqlite3_stmt* q = nullptr;
        CHECK(sqlite3_prepare_v2(db, "UPDATE library_meta SET value=? WHERE key='identity_metadata'", -1, &q, nullptr) == SQLITE_OK);
        const auto value = metadata.dump();
        CHECK(sqlite3_bind_text(q, 1, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK);
        CHECK(sqlite3_step(q) == SQLITE_DONE); sqlite3_finalize(q);
    }
    std::string Scalar(const char* sql) {
        sqlite3_stmt* q = nullptr; CHECK(sqlite3_prepare_v2(db, sql, -1, &q, nullptr) == SQLITE_OK);
        CHECK(sqlite3_step(q) == SQLITE_ROW); const auto* p = sqlite3_column_text(q, 0);
        std::string result = p ? reinterpret_cast<const char*>(p) : ""; sqlite3_finalize(q); return result;
    }
    json Rows(const std::string& sql) {
        sqlite3_stmt* raw = nullptr;
        CHECK(sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr) == SQLITE_OK);
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> q(raw, sqlite3_finalize);
        auto rows = json::array();
        int rc;
        while ((rc = sqlite3_step(q.get())) == SQLITE_ROW) {
            auto row = json::array();
            for (int i = 0; i < sqlite3_column_count(q.get()); ++i) {
                const auto* value = sqlite3_column_text(q.get(), i);
                row.push_back(value ? json(reinterpret_cast<const char*>(value)) : json());
            }
            rows.push_back(std::move(row));
        }
        CHECK(rc == SQLITE_DONE); return rows;
    }
};
void Stop(PlayerLibraryStore& store) {
    store.Shutdown();
    for (int i = 0; i < 1500 && !store.IsStopped(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(store.IsStopped());
}
Result Wait(PlayerLibraryStore& store, RequestId id) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto& r : store.Drain()) if (r.requestId == id) return r;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Worker timed out");
}
void ReviewRegressions(const fs::path& dir) {
    std::vector<std::string> failures;
    auto fixture = [&](const char* tag) { return Options{dir / (std::string(tag) + ".db"), dir / (std::string(tag) + ".ini"), dir / (std::string(tag) + ".json"), false}; };
    auto run = [&](const char* name, auto action) { try { action(); std::cout << "review regression passed: " << name << '\n'; } catch (const std::exception& e) { failures.push_back(std::string(name) + ": " + e.what()); } };
    auto row = [](const std::string& id, json names, json games, json adventure = json::array()) {
        json result = {{"names", names}, {"gameIds", games}, {"adventureGroupIds", adventure}};
        if (!id.empty()) result["entityId"] = id; return result;
    };
    run("imported alias survives split", [&] {
        PlayerLibraryDatabase db(fixture("review-alias")); db.Initialize();
        auto s = db.ReplaceLegacy({{L"A", {L"X"}}, {L"B", {L"Y"}}});
        s = db.ExecuteIdentity({{"action", "merge"}, {"revision", s->revision}, {"names", {"A", "B"}}});
        const auto group = s->identityState["groups"][0]["groupId"];
        s = db.ImportV2(json::array({row("", {"A", "C"}, {"X", "Y"})}));
        const auto names = s->identityMetadata["groups"][0]["names"];
        CHECK(std::find(names.begin(), names.end(), json("C")) != names.end());
        s = db.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", group}, {"splitAll", true}});
        CHECK(s->legacy.size() == 3 && s->FindName(L"A") && s->FindName(L"B") && s->FindName(L"C"));
        CHECK(s->legacy.at(L"C").size() == 2); CHECK(db.LoadSnapshot()->legacy == s->legacy);
    });
    run("rename preserves identity and snapshots", [&] {
        PlayerLibraryDatabase db(fixture("review-rename")); db.Initialize();
        auto s = db.ImportV2(json::array({row("C1", {"A"}, {"X"}, {"Guild"})}));
        const auto id = s->FindName(L"A")->entityId; const auto games = s->FindName(L"A")->gameIds;
        s = db.ExecuteIdentity({{"action", "add_alias"}, {"revision", s->revision}, {"sourceName", "A"}, {"newName", "B"}});
        const auto group = s->identityState["groups"][0]["groupId"];
        s = db.ExecuteIdentity({{"action", "cmd_identity_rename_name"}, {"revision", s->revision}, {"name", "A"}, {"newName", "Renamed"}});
        CHECK(!s->FindName(L"A") && s->FindName(L"Renamed")->entityId == id);
        CHECK(s->FindName(L"Renamed")->cloudId == "C1" && s->FindName(L"Renamed")->gameIds == games);
        const auto metadata = s->identityMetadata["groups"][0];
        CHECK(metadata["beforeMerge"].contains("Renamed") && !metadata["beforeMerge"].contains("A"));
        CHECK(!metadata.contains("beforeAdventure") && metadata["beforeEntities"].contains("Renamed"));
        Reject([&] { db.ExecuteIdentity({{"action", "rename_name"}, {"revision", s->revision}, {"name", "Renamed"}, {"newName", "B"}}); });
        Reject([&] { db.ExecuteIdentity({{"action", "rename_name"}, {"revision", s->revision - 1}, {"name", "Renamed"}, {"newName", "Old"}}); });
        s = db.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", group}, {"splitAll", true}});
        CHECK(!s->FindName(L"A") && s->FindName(L"Renamed")->cloudId == "C1");
        CHECK(s->FindName(L"Renamed")->gameIds == games);
        CHECK(db.LoadSnapshot()->legacy == s->legacy);
    });
    run("reject conflicting cloud ownership", [&] {
        PlayerLibraryDatabase db(fixture("review-cloud-conflict")); db.Initialize();
        auto s = db.ImportV2(json::array({row("C1", {"A"}, {"X"})}));
        Reject([&] { db.ImportV2(json::array({row("C2", {"A", "B"}, {"Y"})})); });
        auto after = db.LoadSnapshot(); CHECK(after->revision == s->revision && after->legacy == s->legacy);
        CHECK(after->identifiers.size() == s->identifiers.size()); CHECK(!after->FindName(L"B"));
        CHECK(after->FindName(L"A")->cloudId == "C1");
    });
    run("public pull skips whole conflicting entities without blocking clean rows", [&] {
        PlayerLibraryDatabase db(fixture("review-partial-pull")); db.Initialize();
        auto s = db.ImportV2(json::array({row("C1", {"A"}, {"X"})}));
        auto payload = json{{"importSource", "https://test/library"}, {"revision", 4}, {"skipOwnershipConflicts", true},
            {"entities", json::array({row("C2", {"A", "BadAlias"}, {"BadId"}), row("C3", {"Safe"}, {"SafeId"})})}};
        s = db.ImportV2(payload);
        CHECK(s->FindName(L"Safe") && !s->FindName(L"BadAlias"));
        CHECK(s->legacy.at(L"A") == std::vector<std::wstring>{L"X"});
        CHECK(s->Lookup(L"BadId").candidates.empty());
        const auto revision = s->revision;
        CHECK(db.ImportV2(payload)->revision == revision);
        s = db.ExecuteIdentity({{"action", "delete_alias"}, {"name", "A"}, {"revision", revision}});
        s = db.ImportV2(payload);
        CHECK(s->FindName(L"BadAlias") && s->FindName(L"A")->cloudId == "C2");
    });
    run("partial ownership planning has no input-order winner", [&] {
        for (bool reverse : {false, true}) {
            PlayerLibraryDatabase db(fixture(reverse ? "review-plan-reverse" : "review-plan-forward")); db.Initialize();
            auto s = db.ReplaceLegacy({{L"A", {L"X"}}, {L"B", {L"Y"}}});
            s = db.ExecuteIdentity({{"action", "merge"}, {"names", {"A", "B"}}, {"revision", s->revision}});
            auto entities = json::array({row("C1", {"A"}, {"Bad1"}), row("C2", {"B"}, {"Bad2"}), row("C3", {"Safe"}, {"Safe"})});
            if (reverse) std::reverse(entities.begin(), entities.end());
            s = db.ImportV2({{"skipOwnershipConflicts", true}, {"entities", entities}});
            CHECK(s->FindName(L"Safe") && s->legacy.at(L"A").size() == 2);
            CHECK(s->Lookup(L"Bad1").candidates.empty() && s->Lookup(L"Bad2").candidates.empty());
        }
    });
    run("admin redirects accept locally merged and distinct prior public owners", [&] {
        for (bool merged : {false, true}) {
            const auto options = fixture(merged ? "review-redirect-merged" : "review-redirect-distinct");
            const auto payload = json{{"importSource", "https://test/library"}, {"revision", 9}, {"skipOwnershipConflicts", true},
                {"entityRedirects", {{{"fromEntityId", "C1"}, {"toEntityId", "Mid"}}, {{"fromEntityId", "Mid"}, {"toEntityId", "C2"}}}},
                {"entities", json::array({row("C2", {"A", "B", "NewAlias"}, {"X", "Y", "Z"})})}};
            std::uint64_t revision;
            {
                PlayerLibraryDatabase db(options); db.Initialize();
                auto s = db.ImportV2(json::array({row("C1", {"A"}, {"X"}), row("C2", {"B"}, {"Y"})}));
                if (merged) s = db.ExecuteIdentity({{"action", "merge"}, {"names", {"A", "B"}}, {"revision", s->revision}});
                s = db.ImportV2(payload);
                CHECK(s->FindName(L"NewAlias") && s->legacy.at(L"A").size() == 3 && s->legacy.at(L"B").size() == 3);
                revision = s->revision;
                CHECK(db.ImportV2(payload)->revision == revision);
            }
            PlayerLibraryDatabase reopened(options); reopened.Initialize();
            CHECK(reopened.ImportV2(payload)->revision == revision);
        }
    });
    run("partial report is committed and malformed batches remain atomic", [&] {
        const auto options = fixture("review-partial-report");
        PlayerLibraryDatabase db(options); db.Initialize();
        auto s = db.ImportV2(json::array({row("C1", {"A"}, {"X"})}));
        const auto revision = s->revision;
        const auto payload = json{{"importSource", "https://test/library"}, {"revision", 5}, {"skipOwnershipConflicts", true},
            {"entities", json::array({row("C2", {"A", "Bad"}, {"BadId"})})}};
        ImportReport report;
        s = db.ImportV2(payload, &report);
        CHECK(s->revision == revision && report.acceptedEntities == 0 && report.skipped.size() == 1);
        CHECK(report.skipped[0].entityId == "C2" && report.skipped[0].names.size() == 2);
        auto invalid = payload;
        invalid["entities"].push_back({{"entityId", "C3"}, {"names", {"Safe"}}, {"gameIds", {123}}});
        Reject([&] { db.ImportV2(invalid, &report); });
        CHECK(report.acceptedEntities == 0 && report.skipped.empty());
        CHECK(db.LoadSnapshot()->revision == revision && !db.LoadSnapshot()->FindName(L"Safe"));
        auto stale = payload; stale["revision"] = 4;
        Reject([&] { db.ImportV2(stale); });
        auto badRedirect = payload;
        for (auto mappings : {json::array({{{"fromEntityId", "C1"}, {"toEntityId", "Missing"}}}),
            json::array({{{"fromEntityId", "C1"}, {"toEntityId", "C4"}}, {{"fromEntityId", "C4"}, {"toEntityId", "C1"}}}),
            json::array({{{"fromEntityId", "C1"}, {"toEntityId", "C2"}}, {{"fromEntityId", "C1"}, {"toEntityId", "C2"}}})}) {
            badRedirect["entityRedirects"] = mappings;
            Reject([&] { db.ImportV2(badRedirect); });
            CHECK(db.LoadSnapshot()->revision == revision);
        }
    });
    run("all-skipped pull cannot trigger unrelated automatic unions", [&] {
        PlayerLibraryDatabase db(fixture("review-all-skipped-no-union")); db.Initialize();
        db.ImportV2(json::array({row("C1", {"A"}, {"X", "Y", "Z", "U", "V"})}));
        const auto before = db.ReplaceLegacy({{L"A", {L"X", L"Y", L"Z", L"U", L"V"}}, {L"B", {L"X", L"Y", L"Z", L"U", L"V"}}});
        CHECK(before->entities.size() == 2);
        ImportReport report;
        const auto after = db.ImportV2({{"skipOwnershipConflicts", true}, {"entities", json::array({row("C2", {"A"}, {"Bad"})})}}, &report);
        CHECK(report.acceptedEntities == 0 && report.skipped.size() == 1);
        CHECK(after->revision == before->revision && after->entities.size() == before->entities.size());
        CHECK(after->identityMetadata == before->identityMetadata);
    });
    run("partial import commit failure rolls back data and remote watermark", [&] {
        const auto options = fixture("review-partial-rollback");
        PlayerLibraryDatabase db(options); db.Initialize();
        auto s = db.ImportV2(json::array({row("C1", {"A"}, {"X"})}));
        auto payload = json{{"importSource", "https://test/library"}, {"revision", 8}, {"skipOwnershipConflicts", true},
            {"entities", json::array({row("C2", {"A", "Bad"}, {"BadId"}), row("C3", {"Safe"}, {"SafeId"})})}};
        ImportReport report;
        {
            Connection guard(options.databasePath);
            guard.Exec("CREATE TRIGGER fail_partial BEFORE INSERT ON player_names BEGIN SELECT RAISE(ABORT, 'test failure'); END;");
            Reject([&] { db.ImportV2(payload, &report); });
            CHECK(report.acceptedEntities == 0 && report.skipped.empty());
            CHECK(db.LoadSnapshot()->legacy == s->legacy && db.LoadSnapshot()->revision == s->revision);
            guard.Exec("DROP TRIGGER fail_partial;");
        }
        payload["revision"] = 7;
        s = db.ImportV2(payload, &report);
        CHECK(s->FindName(L"Safe") && report.acceptedEntities == 1 && report.skipped.size() == 1);
        PlayerLibraryStore store(options);
        CHECK(Wait(store, store.Initialize()).ok);
        const auto result = Wait(store, store.ImportV2(payload));
        CHECK(result.ok && result.importReport.acceptedEntities == 1 && result.importReport.skipped.size() == 1);
        Stop(store);
    });
    run("historical-only redirect preserves unrelated live cloud and split originals", [&] {
        const auto options = fixture("review-historical-redirect");
        const auto payload = json{{"importSource", "https://test/library"}, {"revision", 2}, {"skipOwnershipConflicts", true},
            {"entityRedirects", {{{"fromEntityId", "C1"}, {"toEntityId", "C2"}}}},
            {"entities", json::array({row("C2", {"A"}, {"X", "Y"}), row("C9", {"Holder"}, {"H"})})}};
        {
            PlayerLibraryDatabase db(options); db.Initialize();
            auto s = db.ImportV2(json::array({row("C9", {"Holder"}, {"H"}), row("C1", {"A"}, {"X"})}));
            s = db.ExecuteIdentity({{"action", "merge"}, {"names", {"A", "Holder"}}, {"revision", s->revision}});
            CHECK(s->FindName(L"A")->cloudId == "C9");
            ImportReport report;
            s = db.ImportV2(payload, &report);
            CHECK(report.skipped.empty() && s->FindName(L"A")->cloudId == "C9");
            CHECK(s->identityMetadata["groups"][0]["beforeEntities"]["A"]["cloudId"] == "C1");
            s = db.ExecuteIdentity({{"action", "unmerge"}, {"groupId", s->identityState["groups"][0]["groupId"]},
                {"splitAll", true}, {"keepNewIdsWith", "A"}, {"revision", s->revision}});
            CHECK(s->FindName(L"A")->cloudId == "C1" && s->FindName(L"Holder")->cloudId == "C9");
        }
        PlayerLibraryDatabase reopened(options); reopened.Initialize();
        ImportReport report;
        auto s = reopened.ImportV2(payload, &report);
        CHECK(report.skipped.empty() && s->FindName(L"A")->entityId != s->FindName(L"Holder")->entityId);
        CHECK(s->FindName(L"A")->cloudId == "C1" && s->FindName(L"Holder")->cloudId == "C9");
    });
    run("worker commit failure cannot publish partial report or watermark", [&] {
        const auto options = fixture("review-deferred-commit");
        PlayerLibraryStore store(options);
        CHECK(Wait(store, store.Initialize()).ok);
        const auto before = Wait(store, store.ImportV2(json::array({row("C1", {"A"}, {"X"})}))).snapshot;
        auto payload = json{{"importSource", "https://test/library"}, {"revision", 8}, {"skipOwnershipConflicts", true},
            {"entities", json::array({row("C2", {"A"}, {"Bad"}), row("C3", {"Safe"}, {"SafeId"})})}};
        {
            Connection guard(options.databasePath);
            guard.Exec("CREATE TABLE commit_parent(id INTEGER PRIMARY KEY); CREATE TABLE commit_child(parent_id INTEGER REFERENCES commit_parent(id) DEFERRABLE INITIALLY DEFERRED);");
            guard.Exec("CREATE TRIGGER fail_at_commit AFTER INSERT ON player_names BEGIN INSERT INTO commit_child(parent_id) VALUES(999); END;");
            const auto failed = Wait(store, store.ImportV2(payload));
            CHECK(!failed.ok && failed.importReport.skipped.empty() && failed.importReport.acceptedEntities == 0);
            CHECK(failed.snapshot == before && store.GetSnapshot() == before);
            CHECK(guard.Scalar("SELECT COUNT(*) FROM commit_child") == "0");
            guard.Exec("DROP TRIGGER fail_at_commit;");
        }
        payload["revision"] = 7;
        const auto result = Wait(store, store.ImportV2(payload));
        CHECK(result.ok && result.importReport.skipped.size() == 1 && result.snapshot->FindName(L"Safe"));
        Stop(store);
    });
    run("grouped first pull preserves unassigned local owners", [&] {
        const auto options = fixture("review-grouped-first-pull");
        Write(options.legacyPath, "A=(X)\nB=(Y)\n");
        PlayerLibraryDatabase db(options); auto s = db.Initialize();
        const auto a = s->FindName(L"A")->entityId, b = s->FindName(L"B")->entityId;
        CHECK(a != b && !s->FindName(L"A")->cloudId && !s->FindName(L"B")->cloudId);
        s = db.ImportV2({{"importSource", "https://first/library"}, {"revision", 1}, {"entities", json::array({row("C1", {"A", "B"}, {"X", "Y"})})}});
        CHECK(s->entities.size() == 2 && s->legacy.size() == 2);
        CHECK(s->FindName(L"A")->entityId == a && s->FindName(L"B")->entityId == b);
        CHECK(s->legacy.at(L"A").size() == 2 && s->legacy.at(L"B").size() == 2);
        CHECK(s->Lookup(L"X").candidates.size() == 2 && s->Lookup(L"Y").candidates.size() == 2);
        auto reopened = db.LoadSnapshot(); CHECK(reopened->entities.size() == 2 && reopened->legacy == s->legacy);
        const auto revision = s->revision;
        CHECK(db.ImportV2({{"importSource", "https://first/library"}, {"revision", 1}, {"entities", json::array({row("C1", {"A", "B"}, {"X", "Y"})})}})->revision == revision);
        s = db.ExecuteIdentity({{"action", "merge"}, {"revision", s->revision}, {"names", {"A", "B"}}});
        const auto group = s->identityState["groups"][0]["groupId"];
        s = db.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", group}, {"splitAll", true}});
        CHECK(s->FindName(L"A")->entityId != s->FindName(L"B")->entityId);
        s = db.ImportV2(json::array({row("C1", {"A", "B"}, {"X", "Y"})}));
        CHECK(s->FindName(L"A")->entityId != s->FindName(L"B")->entityId);
        CHECK(s->identityState["groups"].empty());
        s = db.ExecuteIdentity({{"action", "merge"}, {"revision", s->revision}, {"names", {"A", "B"}}});
        Reject([&] { db.ImportV2(json::array({row("C1", {"A"}, {"X"}), row("C2", {"B"}, {"Y"})})); });
        CHECK(db.LoadSnapshot()->revision == s->revision);
    });
    run("retired grouped payload cannot merge local owners or assign ambiguous cloud provenance", [&] {
        const auto options = fixture("review-retired-grouped-payload");
        PlayerLibraryDatabase db(options); db.Initialize();
        const auto before = db.ReplaceLegacy({{L"A", {L"1"}}, {L"B", {L"2"}}});
        const auto batch = json::array({row("C1", {"B", "A", "C"}, json::array(), {"Guild1", "Guild2", "Guild3"})});
        const auto s = db.ImportV2(batch);
        CHECK(s->entities.size() == 3 && s->FindName(L"C")->gameIds.empty());
        CHECK(s->FindName(L"A")->entityId == before->FindName(L"A")->entityId);
        CHECK(s->FindName(L"B")->entityId == before->FindName(L"B")->entityId);
        CHECK(s->identifiers.size() == 2 && s->identityState["groups"].empty());
        CHECK(!s->FindName(L"A")->cloudId && !s->FindName(L"B")->cloudId && !s->FindName(L"C")->cloudId);
        CHECK(db.ImportV2(batch)->revision == s->revision);
        PlayerLibraryDatabase reopened(options); CHECK(reopened.Initialize()->v2Entities == s->v2Entities);
    });
    run("grouped game pull retains cloud provenance", [&] {
        const auto options = fixture("review-grouped-cloud-game");
        const auto batch = json::array({row("C1", {"A", "B"}, {"1", "2", "3", "4", "5"})});
        std::uint64_t revision = 0;
        {
            PlayerLibraryDatabase db(options); db.Initialize();
            db.ReplaceLegacy({{L"A", {L"1"}}, {L"B", {L"2"}}});
            auto s = db.ImportV2(batch);
            CHECK(s->entities.size() == 1 && s->legacy.size() == 2U);
            CHECK(s->FindName(L"A")->cloudId == "C1");
            CHECK(s->identityMetadata["groups"][0]["beforeEntities"]["A"]["cloudId"] == "C1");
            revision = s->revision;
            CHECK(db.ImportV2(batch)->revision == revision);
            CHECK(db.ImportV2(s->v2Entities)->revision == revision);
        }
        {
            PlayerLibraryDatabase db(options); auto s = db.Initialize();
            CHECK(s->revision == revision && s->FindName(L"A")->cloudId == "C1");
            s = db.ImportV2(json::array({row("C1", {"NewRemote"}, json::array())}));
            CHECK(s->entities.size() == 1 && s->FindName(L"NewRemote")->entityId == s->FindName(L"A")->entityId);
            const auto group = s->identityState["groups"][0]["groupId"];
            s = db.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", group}, {"splitAll", true}});
            CHECK(s->FindName(L"A")->cloudId == "C1" && !s->FindName(L"B")->cloudId);
            CHECK(s->legacy.count(L"NewRemote"));
            const auto a = s->FindName(L"A")->entityId;
            s = db.ImportV2(json::array({row("C1", {"AfterSplit"}, json::array())}));
            CHECK(s->FindName(L"AfterSplit")->entityId == a);
            CHECK(db.LoadSnapshot()->legacy == s->legacy);
        }
    });
    run("grouped pulls retain distinct cloud IDs through another cloud union", [&] {
        const auto options = fixture("review-grouped-multiple-clouds");
        const auto batch = json::array({row("C1", {"A", "B"}, {"1", "2", "3", "4", "5"}), row("C2", {"C", "D"}, {"1", "2", "3", "4", "5"})});
        std::uint64_t revision = 0;
        {
            PlayerLibraryDatabase db(options); db.Initialize();
            db.ReplaceLegacy({{L"A", {L"1"}}, {L"B", {L"2"}}, {L"C", {L"4"}}, {L"D", {L"5"}}});
            db.ImportV2(json::array({row("Known", {"E"}, {"1", "2", "3", "4", "5"})}));
            auto s = db.ImportV2(batch);
            CHECK(s->entities.size() == 1 && s->FindName(L"A")->cloudId == "Known");
            CHECK(s->identityMetadata["groups"][0]["beforeEntities"]["A"]["cloudId"] == "C1");
            CHECK(s->identityMetadata["groups"][0]["beforeEntities"]["C"]["cloudId"] == "C2");
            revision = s->revision;
            CHECK(db.ImportV2(batch)->revision == revision);
        }
        PlayerLibraryDatabase db(options); auto s = db.Initialize();
        CHECK(db.ImportV2(batch)->revision == revision);
        Reject([&] { db.ImportV2(json::array({row("Unknown", {"B"}, {"Bad"})})); });
        CHECK(db.LoadSnapshot()->revision == revision);
        s = db.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", s->identityState["groups"][0]["groupId"]}, {"splitAll", true}});
        CHECK(s->entities.size() == 5 && s->FindName(L"A")->cloudId == "C1");
        CHECK(s->FindName(L"C")->cloudId == "C2" && s->FindName(L"E")->cloudId == "Known");
        CHECK(!s->FindName(L"B")->cloudId && !s->FindName(L"D")->cloudId);
    });
    run("grouped cloud assignment rolls back with failed commit", [&] {
        const auto options = fixture("review-grouped-cloud-rollback");
        PlayerLibraryDatabase db(options); db.Initialize();
        const auto before = db.ReplaceLegacy({{L"A", {L"1"}}, {L"B", {L"2"}}});
        const auto batch = json::array({row("C1", {"A", "B"}, {"1", "2", "3", "4", "5"})});
        Connection connection(options.databasePath);
        connection.Exec("CREATE TABLE commit_parent(id INTEGER PRIMARY KEY); CREATE TABLE commit_fault(value INTEGER REFERENCES commit_parent(id) DEFERRABLE INITIALLY DEFERRED); CREATE TRIGGER fail_commit AFTER INSERT ON player_names BEGIN INSERT INTO commit_fault VALUES(99); END;");
        Reject([&] { db.ImportV2(batch); });
        const auto after = db.LoadSnapshot();
        CHECK(after->revision == before->revision && after->v2Entities == before->v2Entities);
        CHECK(after->identityMetadata == before->identityMetadata && after->identifiers.size() == before->identifiers.size());
        CHECK(connection.Scalar("SELECT count(*) FROM commit_fault") == "0");
        connection.Exec("DROP TRIGGER fail_commit; DROP TABLE commit_fault; DROP TABLE commit_parent;");
        CHECK(db.ImportV2(batch)->FindName(L"A")->cloudId == "C1");
    });
    for (bool nameMatch : {false, true}) run(nameMatch ? "partial split live cloud accepts named reimport" : "partial split live cloud routes new alias", [&] {
        const auto options = fixture(nameMatch ? "review-live-cloud-named" : "review-live-cloud-new");
        EntityId owner = 0;
        {
            PlayerLibraryDatabase db(options); db.Initialize();
            auto s = db.ImportV2(json::array({row("E", {"A", "B"}, {"1", "2", "3", "4", "5"}), row("F", {"C"}, {"1", "2", "3", "4", "5"})}));
            CHECK(s->entities.size() == 1);
            s = db.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", s->identityState["groups"][0]["groupId"]}, {"names", {"A"}}});
            CHECK(s->FindName(L"A")->cloudId == "E" && s->FindName(L"A")->entityId != s->FindName(L"B")->entityId);
            CHECK(s->identityMetadata["groups"][0]["beforeEntities"]["B"]["cloudId"] == "E");
            owner = s->FindName(L"A")->entityId;
        }
        PlayerLibraryDatabase db(options); auto s = db.Initialize();
        const auto batch = json::array({row("E", nameMatch ? json{"A", "NewA"} : json{"NewA"}, json::array())});
        s = db.ImportV2(batch);
        CHECK(s->FindName(L"NewA")->entityId == owner);
        CHECK(s->FindName(L"B")->entityId != owner && s->legacy.at(L"B").size() == 5);
        CHECK(s->FindName(L"A")->cloudId == "E");
        CHECK(db.ImportV2(batch)->revision == s->revision);
        s = db.ImportV2(json::array({row("F", {"NewF"}, json::array())}));
        CHECK(s->FindName(L"NewF")->entityId == s->FindName(L"C")->entityId);
    });
    for (bool manyNames : {true, false}) run(manyNames ? "normalized export self-import above 32 names" : "normalized export self-import above 256 IDs", [&] {
        PlayerLibraryDatabase db(fixture(manyNames ? "review-large-export-names" : "review-large-export-ids")); db.Initialize();
        auto batch = json::array();
        for (int i = 0; i < (manyNames ? 33 : 2); ++i) {
            auto games = json{"1", "2", "3", "4", "5"}, adventures = json::array();
            if (!manyNames) for (int j = 0; j < 129; ++j) {
                games.push_back("Game" + std::to_string(i) + "_" + std::to_string(j));
                adventures.push_back("Guild" + std::to_string(i) + "_" + std::to_string(j));
            }
            batch.push_back(row("", {"Name" + std::to_string(i)}, games, adventures));
        }
        const auto s = db.ImportV2(batch); CHECK(s->entities.size() == 1);
        CHECK(s->entities[0].names.size() == (manyNames ? 33U : 2U));
        CHECK(s->entities[0].gameIds.size() == (manyNames ? 5U : 263U));
        CHECK(!s->v2Entities[0].contains("adventureGroupIds"));
        const auto imported = db.ImportV2({{"entities", s->v2Entities}});
        CHECK(imported->revision == s->revision && imported->legacy == s->legacy);
        CHECK(imported->v2Entities == s->v2Entities && imported->identityMetadata == s->identityMetadata);
        CHECK(db.LoadSnapshot()->v2Entities == s->v2Entities);
        for (const auto* field : {"names", "gameIds"}) {
            auto oversized = row("", {"TooLarge"}, json::array());
            oversized[field] = std::vector<std::string>(10001, "Repeated");
            Reject([&] { db.ImportV2(json::array({oversized})); });
            CHECK(db.LoadSnapshot()->revision == s->revision);
        }
    });
    run("remote revision and no-op imports", [&] {
        auto options = fixture("review-remote-revision");
        auto message = [&](int revision, const char* source, const char* name = "A") {
            return json{{"importSource", source}, {"data", {{"revision", revision}, {"entities", json::array({row("", json::array({name}), {"X"})})}}}};
        };
        std::uint64_t localRevision = 0;
        {
            PlayerLibraryDatabase db(options); db.Initialize();
            auto s = db.ImportV2(message(10, "https://one/library")); localRevision = s->revision;
            CHECK(db.ImportV2(message(10, "https://one/library"))->revision == localRevision);
            CHECK(db.ImportV2(message(11, "https://one/library"))->revision == localRevision);
            auto spellingOnly = message(11, "https://one/library");
            spellingOnly["data"]["entities"][0]["gameIds"] = {"X#", "X"};
            CHECK(db.ImportV2(spellingOnly)->revision == localRevision);
            Reject([&] { db.ImportV2(message(10, "https://one/library", "Stale")); });
            CHECK(!db.LoadSnapshot()->FindName(L"Stale"));
            Connection connection(options.databasePath);
            connection.Exec("CREATE TABLE commit_parent(id INTEGER PRIMARY KEY); CREATE TABLE commit_fault(value INTEGER REFERENCES commit_parent(id) DEFERRABLE INITIALLY DEFERRED); CREATE TRIGGER fail_commit AFTER INSERT ON player_names BEGIN INSERT INTO commit_fault VALUES(99); END;");
            Reject([&] { db.ImportV2(message(20, "https://one/library", "Uncommitted")); });
            connection.Exec("DROP TRIGGER fail_commit; DROP TABLE commit_fault; DROP TABLE commit_parent;");
            CHECK(!db.LoadSnapshot()->FindName(L"Uncommitted"));
            s = db.ImportV2(message(12, "https://one/library", "Committed"));
            CHECK(s->FindName(L"Committed")); localRevision = s->revision;
        }
        {
            PlayerLibraryDatabase db(options); auto s = db.Initialize(); CHECK(s->revision == localRevision);
            Reject([&] { db.ImportV2(message(11, "https://one/library", "Stale")); });
            s = db.ImportV2(message(1, "https://two/library", "Independent")); CHECK(s->FindName(L"Independent"));
            s = db.ImportV2({{"players", {{"Legacy", "(New)"}}}}); CHECK(s->FindName(L"Legacy"));
            CHECK(db.ImportV2({{"players", {{"Legacy", "(New)"}}}})->revision == s->revision);
        }
    });
    run("partial split restores original entity grouping", [&] {
        PlayerLibraryDatabase db(fixture("review-partial-split")); db.Initialize();
        auto s = db.ImportV2(json::array({row("E", {"A", "B"}, {"X"}, {"Guild"}), row("", {"C"}, {"Y"})}));
        s = db.ExecuteIdentity({{"action", "merge"}, {"revision", s->revision}, {"names", {"A", "C"}}});
        const auto group = s->identityState["groups"][0]["groupId"];
        s = db.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", group}, {"names", {"A", "B"}}, {"splitAll", false}});
        CHECK(s->FindName(L"A")->entityId == s->FindName(L"B")->entityId);
        CHECK(s->FindName(L"A")->entityId != s->FindName(L"C")->entityId);
        CHECK(s->FindName(L"A")->cloudId == "E" && !s->FindName(L"C")->cloudId);
        CHECK(db.LoadSnapshot()->legacy == s->legacy);
    });
    run("malformed metadata cannot break fallback", [&] {
        auto options = fixture("review-invalid-metadata");
        Write(options.legacyPath, "A=(X)\nB=(Y)\n");
        json metadata = {{"groups", json::array({{{"groupId", "bad-source"}, {"source", 7}, {"names", {"A", "B"}}}})}};
        Write(options.identityGroupsPath, metadata.dump());
        PlayerLibraryDatabase db(options); auto s = db.Initialize(); CHECK(s->persisted && s->legacy.size() == 2);
        CHECK(!db.GetWarning().empty() && s->identityMetadata == json({{"autoGroupPolicyVersion", 5}}));
        auto fallback = db.LoadLegacyFallback(); CHECK(fallback->legacy.size() == 2 && !fallback->persisted);
        fallback = BuildFallbackSnapshot({{L"A", {L"X"}}, {L"B", {L"Y"}}}, metadata);
        CHECK(fallback->legacy.size() == 2 && fallback->identityMetadata.empty());
        metadata["groups"][0]["source"] = "manual";
        metadata["groups"][0]["beforeEntities"] = {{"A", {{"entityId", "not-integer"}}}};
        Reject([&] { db.ReplaceLegacy(s->legacy, metadata); });
        CHECK(db.LoadSnapshot()->revision == s->revision);
    });
    run("explicit ID rename and committed command echo", [&] {
        PlayerLibraryStore store(fixture("review-rename-id"));
        auto result = Wait(store, store.Initialize()); CHECK(result.ok && result.command.is_null());
        result = Wait(store, store.SubmitLegacy({{L"A", {L"X"}}, {L"B", {L"X"}}})); CHECK(result.ok && result.command.is_null());
        result = Wait(store, store.ImportV2(json::array({row("RenameCloud", {"A"}, {"X"}, {"RenameGuild"})})));
        CHECK(result.ok && result.command.is_null());
        result = Wait(store, store.ExecuteIdentity({{"action", "merge"}, {"revision", result.snapshot->revision}, {"names", {"A", "B"}}}));
        CHECK(result.ok);
        const json command = {{"action", "rename_id"}, {"revision", result.snapshot->revision}, {"name", "A"}, {"oldId", "X"}, {"newId", "Y"}};
        result = Wait(store, store.ExecuteIdentity(command)); CHECK(result.ok && result.command == command);
        CHECK(result.snapshot->legacy.at(L"A") == std::vector<std::wstring>{L"Y"});
        CHECK(result.snapshot->legacy.at(L"B") == std::vector<std::wstring>{L"Y"});
        CHECK(result.snapshot->FindName(L"A")->cloudId == "RenameCloud");
        auto old = result.snapshot;
        result = Wait(store, store.ExecuteIdentity(command)); CHECK(!result.ok && result.command.is_null());
        CHECK(result.snapshot->revision == old->revision && result.snapshot->legacy == old->legacy);
        result = Wait(store, store.RequestRefresh()); CHECK(result.ok && result.command.is_null());
        Stop(store);
    });
    if (!failures.empty()) {
        for (const auto& failure : failures) std::cerr << "review regression failed: " << failure << '\n';
        throw std::runtime_error("Reviewer regressions failed");
    }
}
void ThresholdRegressions(const fs::path& dir) {
    auto fixture = [&](const std::string& tag) { return Options{dir / (tag + ".db"), dir / (tag + ".ini"), dir / (tag + ".json"), false}; };
    auto row = [](const char* name, json games, json adventures) {
        return json{{"entityId", std::string("cloud-") + name}, {"names", {name}}, {"gameIds", games}, {"adventureGroupIds", adventures}};
    };
    for (int games : {1, 4, 5}) for (int adventures : {0, 2, 3}) {
        PlayerLibraryDatabase db(fixture("threshold-" + std::to_string(games) + "-" + std::to_string(adventures))); db.Initialize();
        json ids = json::array(), guilds = json::array();
        for (int i = 1; i <= games; ++i) { ids.push_back(std::to_string(i)); ids.push_back(std::to_string(i) + "#"); }
        for (int i = 1; i <= adventures; ++i) { guilds.push_back(std::to_string(i)); guilds.push_back(std::to_string(i)); }
        const auto payload = json{{"importSource", "public-threshold"}, {"revision", 1}, {"skipOwnershipConflicts", true},
            {"entities", json::array({row("A", ids, guilds), row("B", ids, guilds)})}};
        ImportReport report; auto s = db.ImportV2(payload, &report);
        const bool strong = games >= 5;
        CHECK(report.acceptedEntities == 2 && report.skipped.empty());
        CHECK((s->FindName(L"A")->entityId == s->FindName(L"B")->entityId) == strong);
        CHECK(s->identifiers.size() == static_cast<std::size_t>(games));
        CHECK(db.ImportV2(payload)->revision == s->revision);
        CHECK(db.LoadSnapshot()->entities.size() == (strong ? 1u : 2u));
        if (!strong) {
            const auto b = *s->FindName(L"B");
            s = db.ExecuteIdentity({{"action", "update_ids"}, {"revision", s->revision}, {"name", "A"}, {"ids", {"replacement"}}});
            CHECK(s->FindName(L"B")->gameIds == b.gameIds);
            CHECK(s->Lookup(L"1").candidates == std::vector<EntityId>{b.entityId});
            s = db.ExecuteIdentity({{"action", "delete_alias"}, {"revision", s->revision}, {"name", "A"}});
            CHECK(!s->FindName(L"A") && s->FindName(L"B")->gameIds == b.gameIds);
            CHECK(db.LoadSnapshot()->FindName(L"B")->gameIds == b.gameIds);
        }
    }
    {
        PlayerLibraryDatabase db(fixture("threshold-manual-extension")); db.Initialize();
        auto s = db.ReplaceLegacy({{L"A", {L"1", L"2", L"3", L"4", L"5"}}, {L"B", {L"1", L"2", L"3", L"4", L"5"}}});
        s = db.ExecuteIdentity({{"action", "merge"}, {"revision", s->revision}, {"names", {"A", "B"}}});
        CHECK(s->identityMetadata["groups"][0]["source"] == "manual");
        s = db.ImportV2(json::array({row("C", {"1", "2", "3", "4", "5"}, json::array())}));
        CHECK(s->entities.size() == 1);
        CHECK(s->identityMetadata["groups"][0]["source"] == "manual");
        CHECK(db.LoadSnapshot()->identityMetadata == s->identityMetadata);
    }
    std::cout << "Five-game public shared-ID boundary and relation regressions passed.\n";
}
void ThresholdMigrationRegressions(const fs::path& dir) {
    auto fixture = [&](const std::string& tag) { return Options{dir / (tag + ".db"), dir / (tag + ".ini"), dir / (tag + ".json"), false}; };
    auto row = [](json names, json games, const char* cloud) {
        return json{{"entityId", cloud}, {"names", names}, {"gameIds", games}, {"adventureGroupIds", {"g1", "g2"}}};
    };
    for (int scenario : {0, 1, 2, 3, 4}) {
        const bool deferred = scenario != 0;
        const auto options = fixture("threshold-migration-" + std::to_string(scenario));
        std::uint64_t revision = 0; EntityId originalA = 0, originalB = 0;
        json manual;
        {
            PlayerLibraryDatabase db(options); db.Initialize();
            auto s = db.ImportV2(json::array({row({"A", "AliasA"}, {"1", "2", "3", "4", "a"}, "CloudA"), row({"B"}, {"1", "2", "3", "4", "b"}, "CloudB")}));
            originalA = s->FindName(L"A")->entityId; originalB = s->FindName(L"B")->entityId;
            s = db.ExecuteIdentity({{"action", "merge"}, {"revision", s->revision}, {"names", {"A", "B"}}});
            if (scenario == 2 || scenario == 3) {
                const auto ids = scenario == 2 ? json{"1", "2", "3", "4", "a", "b", "new"} : json{"1", "2", "3", "a", "b"};
                s = db.ExecuteIdentity({{"action", "update_ids"}, {"revision", s->revision}, {"name", "A"}, {"ids", ids}});
            }
            s = db.ImportV2(json::array({row({"ManualA"}, {"m"}, "ManualCloudA"), row({"ManualB"}, {"n"}, "ManualCloudB")}));
            s = db.ExecuteIdentity({{"action", "merge"}, {"revision", s->revision}, {"names", {"ManualA", "ManualB"}}});
            Connection connection(options.databasePath);
            auto metadata = json::parse(connection.Scalar("SELECT value FROM library_meta WHERE key='identity_metadata'"));
            metadata["autoGroupPolicyVersion"] = 3;
            for (auto& group : metadata["groups"]) {
                if (group["names"][0] == "A") {
                    group["source"] = "automatic";
                    if (scenario == 1) { group.erase("beforeMerge"); group.erase("beforeAdventure"); group.erase("beforeEntities"); }
                    // An old grouped first cloud pull only tagged its anchor.
                    if (scenario == 4) group["beforeEntities"]["B"].erase("cloudId");
                } else manual = group;
            }
            connection.SetMetadata(metadata); revision = s->revision;
        }
        {
            PlayerLibraryDatabase db(options); auto s = db.Initialize();
            CHECK(s->identityMetadata["autoGroupPolicyVersion"] == 5 && s->revision == revision + 1);
            CHECK(s->FindName(L"A")->entityId == s->FindName(L"AliasA")->entityId);
            CHECK(s->FindName(L"ManualA")->entityId == s->FindName(L"ManualB")->entityId);
            Connection connection(options.databasePath);
            const auto encoded = json::parse(connection.Scalar("SELECT value FROM library_meta WHERE key='identity_metadata'"));
            CHECK(std::find(encoded["groups"].begin(), encoded["groups"].end(), manual) != encoded["groups"].end());
            if (deferred) {
                CHECK(s->FindName(L"A")->entityId == s->FindName(L"B")->entityId);
                CHECK(db.GetWarning().find("需要人工复核") != std::string::npos);
                CHECK(s->identityMetadata["groups"][0]["autoGroupNeedsReview"] == true);
            } else {
                CHECK(s->FindName(L"A")->entityId == originalA && s->FindName(L"B")->entityId == originalB);
                CHECK(s->FindName(L"A")->cloudId == "CloudA" && s->FindName(L"B")->cloudId == "CloudB");
                CHECK(s->legacy.at(L"A") == std::vector<std::wstring>({L"1", L"2", L"3", L"4", L"a"}));
                CHECK(s->legacy.at(L"B") == std::vector<std::wstring>({L"1", L"2", L"3", L"4", L"b"}));
            }
            // The old union has six IDs, but each original only shares four with C.
            s = db.ImportV2(json::array({row({"C"}, {"1", "2", "3", "a", "b"}, "CloudC")}));
            CHECK(s->FindName(L"C")->entityId != s->FindName(L"A")->entityId && s->FindName(L"C")->entityId != s->FindName(L"B")->entityId);
            revision = s->revision;
        }
        PlayerLibraryDatabase reopened(options); auto s = reopened.Initialize();
        CHECK(s->revision == revision);
        CHECK(s->FindName(L"C")->entityId != s->FindName(L"A")->entityId);
        if (deferred) CHECK(!reopened.GetWarning().empty());
    }
    {
        const auto options = fixture("threshold-legacy");
        Write(options.legacyPath, "A=(1)(2)(3)(a)(b)\nB=(1)(2)(3)(a)(b)\n");
        Write(options.identityGroupsPath, json{{"autoGroupPolicyVersion", 3}, {"groups", json::array({
            {{"groupId", "old-auto"}, {"source", "automatic"}, {"names", {"A", "B"}},
             {"beforeMerge", {{"A", {"1", "2", "3", "a"}}, {"B", {"1", "2", "3", "b"}}}}}
        })}}.dump());
        PlayerLibraryDatabase db(options); auto s = db.Initialize();
        CHECK(s->FindName(L"A")->entityId != s->FindName(L"B")->entityId);
        CHECK(s->legacy.at(L"A").size() == 4 && s->legacy.at(L"B").size() == 4);
        CHECK(s->identityState["groups"].empty() && s->identityMetadata["autoGroupPolicyVersion"] == 5);
    }
    {
        const auto options = fixture("threshold-strong-migration");
        std::uint64_t revision = 0;
        {
            PlayerLibraryDatabase db(options); db.Initialize();
            const auto s = db.ImportV2(json::array({row({"A"}, {"1", "2", "3", "4", "5", "a"}, "CloudA"), row({"B"}, {"1", "2", "3", "4", "5", "b"}, "CloudB")}));
            Connection connection(options.databasePath);
            auto metadata = json::parse(connection.Scalar("SELECT value FROM library_meta WHERE key='identity_metadata'"));
            metadata.erase("autoGroupPolicyVersion"); connection.SetMetadata(metadata); revision = s->revision;
            connection.Exec("CREATE TRIGGER fail_migration BEFORE INSERT ON player_names BEGIN SELECT RAISE(ABORT,'migration rollback'); END");
        }
        {
            PlayerLibraryDatabase db(options); Reject([&] { db.Initialize(); });
            Connection connection(options.databasePath);
            CHECK(connection.Scalar("SELECT value FROM library_meta WHERE key='revision'") == std::to_string(revision));
            CHECK(!json::parse(connection.Scalar("SELECT value FROM library_meta WHERE key='identity_metadata'")).contains("autoGroupPolicyVersion"));
            CHECK(connection.Scalar("SELECT count(*) FROM player_entities WHERE deleted=0") == "1");
            connection.Exec("DROP TRIGGER fail_migration");
        }
        PlayerLibraryDatabase db(options); auto s = db.Initialize();
        CHECK(s->entities.size() == 1 && s->legacy.at(L"A").size() == 7 && s->revision == revision + 1);
        CHECK(s->identityMetadata["groups"][0]["beforeMerge"]["A"].size() == 6);
        CHECK(s->identityMetadata["autoGroupPolicyVersion"] == 5 && db.GetWarning().empty());
        CHECK(db.Initialize()->revision == s->revision);
    }
    std::cout << "Old automatic policy migration regressions passed.\n";
}
void DenseThresholdRegressions(const fs::path& dir) {
    for (bool ignored : {false, true}) {
        const auto tag = std::string("dense-threshold-") + "game" + (ignored ? "-ignored" : "");
        const Options options{dir / (tag + ".db"), dir / (tag + ".ini"), dir / (tag + ".json"), false};
        std::uint64_t revision = 0;
        auto batch = json::array();
        for (int i = 0; i < 257; ++i) {
            auto ids = json::array();
            for (int id = 1; id <= 5; ++id) ids.push_back("shared-" + std::to_string(id));
            ids.push_back("own-" + std::to_string(i));
            batch.push_back({{"names", {"Player-" + std::to_string(i)}}, {"gameIds", ids},
                {"adventureGroupIds", json::array()}});
        }
        {
            PlayerLibraryDatabase db(options); db.Initialize();
            if (ignored) {
                auto s = db.ReplaceLegacy({{L"UnrelatedA", {L"unrelated"}}, {L"UnrelatedB", {L"unrelated"}}});
                db.ExecuteIdentity({{"action", "ignore_overlap"}, {"revision", s->revision}, {"leftName", "UnrelatedA"}, {"rightName", "UnrelatedB"}});
            }
            auto s = db.ImportV2(batch);
            CHECK(s->FindName(L"Player-0")->names.size() == 257);
            CHECK(s->identityMetadata["groups"][0]["beforeMerge"].size() == 257);
            CHECK(s->identityMetadata["groups"][0]["beforeMerge"]["Player-0"].size() == 6u);
            CHECK(db.ImportV2(batch)->revision == s->revision);
            // The merged union contains these IDs, but no constituent has five/three of them.
            auto unionOnly = json::array();
            for (int i = 0; i < 5; ++i) unionOnly.push_back("own-" + std::to_string(i));
            s = db.ImportV2(json::array({{{"names", {"UnionOnly"}}, {"gameIds", unionOnly},
                {"adventureGroupIds", json::array()}}}));
            CHECK(s->FindName(L"UnionOnly")->entityId != s->FindName(L"Player-0")->entityId);
            revision = s->revision;
        }
        PlayerLibraryDatabase db(options); auto s = db.Initialize();
        CHECK(s->revision == revision && s->FindName(L"Player-0")->names.size() == 257);
        CHECK(s->FindName(L"UnionOnly")->entityId != s->FindName(L"Player-0")->entityId);
        CHECK(db.ImportV2(batch)->revision == revision);
    }
    std::cout << "257-player distinct-set game, unrelated-ignore and original-evidence regressions passed.\n";
}
void RemovedIdentityEvidenceRegressions(const fs::path& dir) {
    {
        const auto tag = std::string("removed-evidence-") + "game";
        const Options options{dir / (tag + ".db"), dir / (tag + ".ini"), dir / (tag + ".json"), false};
        auto shared = json::array();
        for (int i = 0; i < 5; ++i) shared.push_back("old-" + std::to_string(i));
        auto row = [&](const char* name, json ids) {
            return json{{"names", {name}}, {"gameIds", ids},
                {"adventureGroupIds", json::array()}};
        };
        std::uint64_t revision = 0;
        {
            PlayerLibraryDatabase db(options); db.Initialize();
            auto a = shared, b = shared; a.push_back("a-only"); b.push_back("b-only");
            auto s = db.ImportV2(json::array({row("A", a), row("B", b)}));
            CHECK(s->FindName(L"A")->entityId == s->FindName(L"B")->entityId);
            const auto originals = s->identityMetadata["groups"][0]["beforeMerge"];
            const auto group = s->identityMetadata["groups"][0]["groupId"];
            s = db.ExecuteIdentity({{"action", "update_ids"}, {"revision", s->revision},
                {"name", "A"}, {"ids", {"replacement"}}});
            CHECK(s->identityMetadata["groups"][0]["beforeMerge"] == originals);
            s = db.ImportV2(json::array({row("C", shared)}));
            CHECK(s->FindName(L"C")->entityId != s->FindName(L"A")->entityId);
            CHECK(s->identityMetadata["groups"][0]["beforeMerge"] == originals);
            CHECK(s->identityState["overlapSuggestions"].empty());
            s = db.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", group},
                {"splitAll", true}, {"keepNewIdsWith", "A"}});
            const auto* restored = s->FindName(L"B");
            CHECK(restored->gameIds.size() == shared.size() + 1);
            revision = s->revision;
        }
        PlayerLibraryDatabase db(options); auto s = db.Initialize();
        CHECK(s->revision == revision && s->FindName(L"C")->entityId != s->FindName(L"A")->entityId);
    }
    std::cout << "Removed game evidence cannot merge new players; split originals retained.\n";
}
void MatchIdentityRegressions(const fs::path& dir) {
    auto fixture = [&](const char* tag) { return Options{dir / (std::string(tag) + ".db"), dir / (std::string(tag) + ".ini"), dir / (std::string(tag) + ".json"), false}; };
    auto row = [](const char* name, json games, json adventure = json::array(), const char* cloud = "") {
        json r = {{"names", json::array({name})}, {"gameIds", games}, {"adventureGroupIds", adventure}};
        if (*cloud) r["entityId"] = cloud;
        return r;
    };
    PlayerLibraryDatabase db(fixture("match-threshold"));
    db.Initialize();
    auto s = db.ImportV2({{"entities", json::array({
        {{"names", {"A"}}, {"gameIds", {"1", "2", "3", "4", "5", "a"}}},
        {{"names", {"B"}}, {"gameIds", {"1", "2", "3", "4", "5", "b"}}}
    })}});
    CHECK(s->FindName(L"A")->entityId == s->FindName(L"B")->entityId);
    CHECK(s->entities.size() == 1 && s->legacy.at(L"A").size() == 7);
    CHECK(s->Lookup(L"1").candidates.size() == 1 && !s->Lookup(L"1").conflict);
    CHECK(s->identityState["groups"][0]["source"] == "automatic");
    CHECK(s->identityMetadata["groups"][0]["beforeMerge"]["A"].size() == 6);
    CHECK(ParseLegacy(SerializeLegacy(s->legacy)) == s->legacy);
    const auto revision = s->revision;
    CHECK(db.ImportV2({{"entities", json::array()}})->revision == revision);
    s = db.ReplaceLegacy(s->legacy);
    CHECK(s->entities.size() == 1 && s->revision == revision);

    PlayerLibraryDatabase boundary(fixture("match-boundary")); boundary.Initialize();
    s = boundary.ImportV2(json::array({row("TwoA", {"1", "2", "1#"}), row("TwoB", {"1", "2", "2"}),
        row("EmptyA", json::array()), row("EmptyB", json::array()),
        row("OneA", json::array(), {"guild", "guild"}), row("OneB", json::array(), {"guild"})}));
    CHECK(s->entities.size() == 6 && s->identityState["groups"].empty());
    CHECK(s->FindName(L"EmptyA") && s->legacy.at(L"EmptyA").empty());
    Reject([&] { boundary.ExecuteIdentity({{"action", "ignore_overlap"}, {"revision", s->revision}, {"leftName", "OneA"}, {"rightName", "OneB"}}); });
    CHECK(boundary.LoadSnapshot()->revision == s->revision);
    s = boundary.ImportV2(json::array({row("OneA", json::array(), {"guild2", "guild3"}), row("OneB", json::array(), {"guild2", "guild3"})}));
    CHECK(s->FindName(L"OneA")->entityId != s->FindName(L"OneB")->entityId);
    s = boundary.ExecuteIdentity({{"action", "ignore_overlap"}, {"revision", s->revision}, {"leftName", "TwoA"}, {"rightName", "TwoB"}});
    s = boundary.ImportV2(json::array({row("TwoA", {"3", "4", "5"}), row("TwoB", {"3", "4", "5"})}));
    CHECK(s->FindName(L"TwoA")->entityId != s->FindName(L"TwoB")->entityId);
    CHECK(boundary.LoadSnapshot()->identityState["groups"].empty());

    const auto remoteOptions = fixture("match-remote");
    const auto remoteBatch = json::array({row("RemoteA", {"x", "a", "1", "2", "3", "4", "5"}, json::array(), "CloudA"), row("RemoteB", {"y", "b", "1", "2", "3", "4", "5"}, json::array(), "CloudB")});
    json before; std::uint64_t mergedRevision = 0;
    {
        PlayerLibraryDatabase remote(remoteOptions); remote.Initialize();
        s = remote.ImportV2(remoteBatch);
        CHECK(s->entities.size() == 1 && s->FindName(L"RemoteA")->gameIds.size() == 9);
        CHECK(s->Lookup(L"1").candidates.size() == 1);
        before = s->identityMetadata; mergedRevision = s->revision;
        CHECK(remote.ImportV2(remoteBatch)->revision == mergedRevision);
        CHECK(remote.ImportV2(s->v2Entities)->revision == mergedRevision);
        CHECK(remote.ImportV2(json::array({row("RemoteA", json::array(), {"g1", "g2", "g3"}), row("RemoteB", json::array(), {"g1", "g2", "g3"})}))->revision == mergedRevision);
    }
    {
        PlayerLibraryDatabase remote(remoteOptions); s = remote.Initialize();
        CHECK(s->entities.size() == 1 && s->identityMetadata == before);
        CHECK(remote.ImportV2(remoteBatch)->revision == mergedRevision);
        Reject([&] { remote.ImportV2(json::array({row("RemoteA", {"bad"}, json::array(), "UnknownCloud")})); });
        CHECK(remote.LoadSnapshot()->revision == mergedRevision);
        s = remote.ImportV2(json::array({row("RemoteB", {"later"}, json::array(), "CloudB")}));
        CHECK(s->legacy.at(L"RemoteA").size() == 10);
        auto group = s->identityState["groups"][0]["groupId"];
        Reject([&] { remote.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", group}, {"splitAll", true}}); });
        s = remote.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", group}, {"splitAll", true}, {"keepNewIdsWith", "RemoteB"}});
        CHECK(s->entities.size() == 2 && s->FindName(L"RemoteA")->cloudId == "CloudA" && s->FindName(L"RemoteB")->cloudId == "CloudB");
        CHECK(s->legacy.at(L"RemoteA").size() == 7 && s->legacy.at(L"RemoteB").size() == 8);
        s = remote.ImportV2(remoteBatch);
        CHECK(s->entities.size() == 2 && s->identityState["groups"].empty());
        CHECK(remote.LoadSnapshot()->identityState["groups"].empty());
    }
    for (int reverse = 0; reverse < 2; ++reverse) {
        PlayerLibraryDatabase chain(fixture(reverse ? "match-chain-reversed" : "match-chain")); chain.Initialize();
        auto batch = json::array({row("A", {"1", "2", "3", "ab4", "ab5"}), row("B", {"1", "2", "3", "ab4", "ab5", "4", "5", "6", "bc4", "bc5"}), row("C", {"4", "5", "6", "bc4", "bc5"})});
        if (reverse) std::reverse(batch.begin(), batch.end());
        s = chain.ImportV2(batch);
        CHECK(s->entities.size() == 2 && s->FindName(L"A")->entityId == s->FindName(L"B")->entityId);
        CHECK(s->FindName(L"A")->entityId != s->FindName(L"C")->entityId);
        const auto rev = s->revision;
        CHECK(chain.ImportV2(batch)->revision == rev);
        CHECK(chain.LoadSnapshot()->entities.size() == 2);
    }
    {
        PlayerLibraryDatabase partial(fixture("match-partial-cloud")); partial.Initialize();
        s = partial.ImportV2(remoteBatch);
        const auto group = s->identityState["groups"][0]["groupId"];
        s = partial.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", group}, {"names", {"RemoteA"}}});
        CHECK(s->FindName(L"RemoteA")->cloudId == "CloudA" && s->FindName(L"RemoteB")->cloudId == "CloudB");
        CHECK(s->legacy.at(L"RemoteA").size() == 7 && s->legacy.at(L"RemoteB").size() == 7);
        CHECK(partial.ImportV2(remoteBatch)->entities.size() == 2);
    }
    {
        const auto options = fixture("match-rollback"); PlayerLibraryDatabase rollback(options); rollback.Initialize();
        s = rollback.ImportV2(json::array({row("A", {"1", "2"}), row("B", {"1", "2"})}));
        const auto baseline = s;
        Connection connection(options.databasePath);
        connection.Exec("CREATE TABLE commit_parent(id INTEGER PRIMARY KEY); CREATE TABLE commit_fault(value INTEGER REFERENCES commit_parent(id) DEFERRABLE INITIALLY DEFERRED); CREATE TRIGGER fail_commit AFTER INSERT ON player_names BEGIN INSERT INTO commit_fault VALUES(99); END;");
        Reject([&] { rollback.ImportV2({{"importSource", "match-test"}, {"revision", 20}, {"entities", json::array({row("A", {"3", "4", "5"}), row("B", {"3", "4", "5"})})}}); });
        s = rollback.LoadSnapshot();
        CHECK(s->revision == baseline->revision && s->legacy == baseline->legacy && s->identityMetadata == baseline->identityMetadata);
        CHECK(s->identifiers.size() == baseline->identifiers.size() && s->entities.size() == 2);
        connection.Exec("DROP TRIGGER fail_commit; DROP TABLE commit_fault; DROP TABLE commit_parent;");
        s = rollback.ImportV2({{"importSource", "match-test"}, {"revision", 19}, {"entities", json::array({row("A", {"3", "4", "5"}), row("B", {"3", "4", "5"})})}});
        CHECK(s->entities.size() == 1);
    }
    {
        PlayerLibraryDatabase match(fixture("match-cloud-free-update")); match.Initialize();
        s = match.ImportV2(json::array({row("A", {"1", "2"}, json::array(), "ExistingA"), row("B", {"1", "2"}, json::array(), "ExistingB")}));
        CHECK(s->entities.size() == 2);
        s = match.ImportV2({{"entities", json::array({row("A", {"3", "4", "5"}), row("B", {"3", "4", "5"})})}});
        CHECK(s->entities.size() == 1);
        const auto rev = s->revision;
        CHECK(match.ImportV2(json::array({row("A", {"1", "2"}, json::array(), "ExistingA"), row("B", {"1", "2"}, json::array(), "ExistingB")}))->revision == rev);
        CHECK(match.ImportV2(s->v2Entities)->revision == rev);
    }
    {
        PlayerLibraryDatabase mixed(fixture("match-mixed-provenance")); mixed.Initialize();
        s = mixed.ImportV2(json::array({row("A", {"1"}), row("B", {"2"}, json::array(), "Known")}));
        s = mixed.ExecuteIdentity({{"action", "merge"}, {"revision", s->revision}, {"names", {"A", "B"}}});
        Reject([&] { mixed.ImportV2(json::array({row("A", {"bad"}, json::array(), "Unknown")})); });
        CHECK(mixed.ImportV2(json::array({row("B", {"2"}, json::array(), "Known")}))->entities.size() == 1);
    }
    {
        PlayerLibraryDatabase many(fixture("match-large-split")); many.Initialize();
        auto batch = json::array();
        for (int i = 0; i < 33; ++i) batch.push_back(row(("Name" + std::to_string(i)).c_str(), {"1", "2", "3", "4", "5"}));
        s = many.ImportV2(batch); CHECK(s->entities.size() == 1);
        s = many.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", s->identityState["groups"][0]["groupId"]}, {"splitAll", true}});
        CHECK(s->entities.size() == 33);
        CHECK(s->identityMetadata["autoSplitFingerprints"].size() == 1);
        s = many.ExecuteIdentity({{"action", "update_ids"}, {"revision", s->revision}, {"name", "Name0"}, {"ids", json::array()}});
        CHECK(s->identityState["groups"].empty());
        s = many.ExecuteIdentity({{"action", "rename_name"}, {"revision", s->revision}, {"name", "Name1"}, {"newName", "Renamed1"}});
        CHECK(many.ImportV2({{"entities", json::array()}})->entities.size() == 33);
        CHECK(many.LoadSnapshot()->identityState["groups"].empty());
    }
    {
        PlayerLibraryDatabase many(fixture("match-large-import")); many.Initialize();
        auto batch = json::array();
        for (int i = 0; i < 1000; ++i) batch.push_back(row(("Player" + std::to_string(i)).c_str(), {"1", "2", "3", "4", "5"}));
        const auto start = std::chrono::steady_clock::now();
        s = many.ImportV2(batch); CHECK(s->entities.size() == 1 && s->legacy.size() == 1000);
        CHECK(many.ImportV2(batch)->revision == s->revision);
        CHECK(many.LoadSnapshot()->legacy.size() == 1000);
        std::cout << "1000-name automatic union/import/reimport/reload: " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() << "ms\n";
    }
    std::cout << "Match identity regressions passed.\n";
}
void ResetRegressions(const fs::path& dir) {
    std::vector<std::string> failures;
    auto run = [&](const std::string& name, auto action) {
        try { action(); std::cout << "reset regression passed: " << name << '\n'; }
        catch (const std::exception& e) { failures.push_back(name + ": " + e.what()); }
    };
    auto fixture = [&](const std::string& tag) {
        const auto folder = dir / ("reset-" + tag); fs::create_directory(folder);
        return Options{folder / "player_library.db", folder / "alias_db.ini", folder / "groups.json", false};
    };
    auto paths = [](const Options& o) {
        return std::vector<fs::path>{o.legacyPath, o.identityGroupsPath, o.legacyPath.parent_path() / "alias_cloud_baseline.json",
            o.legacyPath.parent_path() / "players_config.txt"};
    };
    auto read = [](const fs::path& path) {
        std::ifstream input(path, std::ios::binary); CHECK(input.good());
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    auto files = [&](const Options& o) {
        auto result = json::array();
        for (const auto& path : paths(o)) result.push_back(fs::exists(path) ? json(read(path)) : json());
        return result;
    };
    auto storage = [](const fs::path& path) {
        Connection connection(path); auto result = json::object();
        for (const auto* table : {"library_meta", "player_entities", "player_names", "identifiers", "identifier_spellings", "entity_identifiers"})
            result[table] = connection.Rows(std::string("SELECT * FROM ") + table + " ORDER BY 1,2");
        CHECK(connection.Scalar("PRAGMA integrity_check") == "ok");
        CHECK(connection.Rows("PRAGMA foreign_key_check").empty());
        return result;
    };
    auto command = [](const SnapshotPtr& s) {
        return json{{"action", "cmd_identity_reset_local"}, {"revision", s->revision}, {"confirmed", true}};
    };
    auto seed = [&](PlayerLibraryDatabase& db) {
        const auto& o = db.GetOptions();
        Write(o.legacyPath, ToUtf8(L"BeforeReset=(Role#Job)(Role\uff03Job)\r\nOther=(OtherRole)\r\nDeleted=(OldRole)\r\n"));
        Write(o.identityGroupsPath, json{{"version", 1}, {"groups", json::array()},
            {"autoSplitFingerprints", {"retired"}}, {"autoSplitNameSets", {{"GhostA", "GhostB"}}}}.dump());
        db.Initialize();
        auto s = db.ImportV2({{"importSource", "reset-primary"}, {"revision", 30}, {"entities", json::array({
            {{"entityId", "OldCloud"}, {"names", {"BeforeReset"}}, {"gameIds", {"Role#Job"}}, {"adventureGroupIds", {"OldGuild"}}}})}});
        s = db.ImportV2({{"importSource", "reset-secondary"}, {"revision", 11}, {"entities", json::array()}});
        s = db.ExecuteIdentity({{"action", "merge"}, {"revision", s->revision}, {"names", {"BeforeReset", "Other"}}});
        s = db.ExecuteIdentity({{"action", "delete_alias"}, {"revision", s->revision}, {"name", "Deleted"}});
        Write(o.legacyPath, SerializeLegacy(s->legacy)); Write(o.identityGroupsPath, s->identityMetadata.dump());
        Write(paths(o)[2], "{\"mainNames\":[\"BeforeReset\"],\"players\":{\"BeforeReset\":\"(Role#Job)\"}}");
        Write(paths(o)[3], "0|BeforeReset|4|1|0|Role#Job\r\n1|Other|1|4|0|OtherRole\r\n");
        Connection connection(o.databasePath);
        CHECK(connection.Scalar("SELECT value FROM library_meta WHERE key='migration_name_count'") == "3");
        CHECK(connection.Scalar("SELECT COUNT(*) FROM player_entities WHERE deleted=1") != "0");
        CHECK(connection.Scalar("SELECT COUNT(*) FROM identifier_spellings") != "0");
        return s;
    };
    auto unchanged = [&](PlayerLibraryDatabase& db, const SnapshotPtr& before, const json& stored, const json& originals) {
        const auto after = db.LoadSnapshot();
        CHECK(after->revision == before->revision && after->persisted);
        CHECK(after->legacy == before->legacy && after->v2Entities == before->v2Entities);
        CHECK(after->identityMetadata == before->identityMetadata && after->identityState == before->identityState);
        CHECK(storage(db.GetOptions().databasePath) == stored);
        CHECK(files(db.GetOptions()) == originals);
        for (const auto& file : fs::directory_iterator(db.GetOptions().databasePath.parent_path()))
            CHECK(file.path().extension() != ".tmp");
    };
    struct LockedFile {
        HANDLE handle;
        LockedFile(const fs::path& path, DWORD sharing) : handle(CreateFileW(path.c_str(), GENERIC_READ,
            sharing, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) { CHECK(handle != INVALID_HANDLE_VALUE); }
        ~LockedFile() { CloseHandle(handle); }
        LockedFile(const LockedFile&) = delete;
        LockedFile& operator=(const LockedFile&) = delete;
    };
    run("reset clears all local state and preserves a complete recovery backup", [&] {
        const auto o = fixture("metadata"); PlayerLibraryDatabase db(o); const auto before = seed(db);
        const auto stored = storage(o.databasePath), originals = files(o);
        const auto after = db.ExecuteIdentity(command(before));
        CHECK(after->persisted && after->revision == before->revision + 1);
        CHECK(after->entities.empty() && after->identifiers.empty() && after->legacy.empty());
        CHECK(after->nameIndex.empty() && after->gameIndex.empty());
        CHECK(after->v2Entities.empty() && after->identityState["groups"].empty());
        CHECK(after->identityMetadata.size() == 2 && after->identityMetadata.contains("localResetBackup"));
        CHECK(after->identityMetadata["autoGroupPolicyVersion"] == dnf::identity::AUTO_GROUP_POLICY_VERSION);
        const fs::path backupFolder = FromUtf8(after->identityMetadata["localResetBackup"].get<std::string>());
        CHECK(backupFolder.parent_path() == o.databasePath.parent_path());
        CHECK(storage(backupFolder / "player_library.db") == stored);
        const auto originalsPaths = paths(o);
        for (std::size_t i = 0; i < originalsPaths.size(); ++i)
            CHECK(read(backupFolder / originalsPaths[i].filename()) == originals[i]);
        CHECK(ParseLegacy(read(o.legacyPath)).empty());
        CHECK(read(originalsPaths[3]).empty());
        const auto groups = json::parse(read(o.identityGroupsPath));
        CHECK(groups["groups"].empty() && groups["autoSplitFingerprints"].empty());
        CHECK(json::parse(read(originalsPaths[2])) == json({{"mainNames", json::array()}, {"players", json::object()}}));
        Connection connection(o.databasePath);
        for (const auto* table : {"player_entities", "player_names", "identifiers", "identifier_spellings", "entity_identifiers"})
            CHECK(connection.Rows(std::string("SELECT * FROM ") + table).empty());
        CHECK(connection.Scalar("SELECT value FROM library_meta WHERE key='remote_revisions'") == "{}");
        CHECK(connection.Rows("SELECT * FROM library_meta WHERE key GLOB 'migration_*'").empty());
        CHECK(connection.Scalar("SELECT value FROM library_meta WHERE key='storage_version'") == "3");
        CHECK(connection.Scalar("SELECT value FROM library_meta WHERE key='normalizer_version'") == "1");
        PlayerLibraryDatabase reopened(o); const auto persisted = reopened.Initialize();
        CHECK(persisted->revision == after->revision && persisted->identityMetadata == after->identityMetadata);
        CHECK(persisted->entities.empty() && persisted->identifiers.empty());
    });
    for (bool reopen : {false, true}) run(reopen ? "import after reset and reopen" : "import immediately after reset", [&] {
        const auto o = fixture(reopen ? "import-reopen" : "import-live");
        auto db = std::make_unique<PlayerLibraryDatabase>(o); const auto before = seed(*db);
        const json payload = {{"importSource", "reset-primary"}, {"revision", 1}, {"entities", json::array({
            {{"entityId", "NewCloud"}, {"names", {"BeforeReset", "NewAlias"}}, {"gameIds", {"Role#Job", "NewRole"}}, {"adventureGroupIds", {"NewGuild"}}}})}};
        Reject([&] { db->ImportV2(payload); });
        const auto empty = db->ExecuteIdentity(command(before));
        if (reopen) { db.reset(); db = std::make_unique<PlayerLibraryDatabase>(o); CHECK(db->Initialize()->revision == empty->revision); }
        auto imported = db->ImportV2(payload);
        CHECK(imported->revision == empty->revision + 1 && imported->entities.size() == 1);
        CHECK(imported->FindName(L"BeforeReset")->cloudId == "NewCloud" && imported->FindName(L"NewAlias"));
        CHECK(!imported->FindName(L"Other") && !imported->FindName(L"Deleted"));
        CHECK(imported->legacy.at(L"BeforeReset") == std::vector<std::wstring>({L"Role#Job", L"NewRole"}));
        CHECK(imported->identifiers.size() == 2 && imported->Lookup(L"OldGuild").candidates.empty());
        CHECK(imported->Lookup(L"NewGuild").candidates.empty());
        CHECK(imported->identityMetadata["localResetBackup"] == empty->identityMetadata["localResetBackup"]);
        CHECK(db->ImportV2(payload)->revision == imported->revision);
        CHECK(db->ImportV2({{"importSource", "reset-secondary"}, {"revision", 1}, {"entities", json::array()}})->revision == imported->revision);
        auto stale = payload; stale["revision"] = 0; Reject([&] { db->ImportV2(stale); });
        db.reset(); PlayerLibraryDatabase reopened(o);
        CHECK(reopened.Initialize()->v2Entities == imported->v2Entities);
        CHECK(reopened.ImportV2(payload)->revision == imported->revision);
        Reject([&] { reopened.ImportV2(stale); });
    });
    for (bool backupRead : {true, false}) for (std::size_t index = 0; index < 4; ++index)
        run(std::string(backupRead ? "backup read failure " : "file replacement rollback ") + std::to_string(index), [&] {
            const auto o = fixture(std::string(backupRead ? "read-" : "write-") + std::to_string(index));
            PlayerLibraryDatabase db(o); const auto before = seed(db);
            const auto stored = storage(o.databasePath), originals = files(o);
            {
                // Sharing permits backup reads but withholds replacement/deletion for write failures.
                LockedFile locked(paths(o)[index], backupRead ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE);
                std::string error;
                try { db.ExecuteIdentity(command(before)); }
                catch (const std::exception& e) { error = e.what(); }
                CHECK(!error.empty());
                if (!backupRead) CHECK(error.find("database is committed") == std::string::npos);
            }
            unchanged(db, before, stored, originals);
            PlayerLibraryDatabase reopened(o); CHECK(reopened.Initialize()->revision == before->revision);
            CHECK(db.ExecuteIdentity(command(before))->entities.empty());
        });
    run("busy database does not rewrite untouched compatibility files", [&] {
        const auto o = fixture("busy"); PlayerLibraryDatabase db(o); const auto before = seed(db);
        const auto stored = storage(o.databasePath), originals = files(o);
        std::vector<fs::file_time_type> times;
        for (const auto& path : paths(o)) {
            fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(24));
            times.push_back(fs::last_write_time(path));
        }
        Connection connection(o.databasePath); connection.Exec("BEGIN IMMEDIATE");
        Reject([&] { db.ExecuteIdentity(command(before)); });
        connection.Exec("ROLLBACK");
        unchanged(db, before, stored, originals);
        const auto originalPaths = paths(o);
        for (std::size_t i = 0; i < originalPaths.size(); ++i) CHECK(fs::last_write_time(originalPaths[i]) == times[i]);
        CHECK(db.ExecuteIdentity(command(before))->entities.empty());
    });
    for (bool missing : {false, true}) for (int phase = 0; phase < 3; ++phase)
        run(std::string(missing ? "absent-file rollback " : "database rollback ") + std::to_string(phase), [&] {
            const auto o = fixture(std::string(missing ? "missing-" : "sql-") + std::to_string(phase));
            PlayerLibraryDatabase db(o); const auto before = seed(db);
            if (missing) for (const auto& path : paths(o)) CHECK(fs::remove(path));
            const auto stored = storage(o.databasePath), originals = files(o);
            Connection connection(o.databasePath);
            if (phase == 0) connection.Exec("CREATE TRIGGER fail_reset BEFORE DELETE ON identifiers BEGIN SELECT RAISE(ABORT,'reset delete rollback'); END");
            else if (phase == 1) connection.Exec("CREATE TRIGGER fail_reset BEFORE UPDATE ON library_meta WHEN NEW.key='identity_metadata' BEGIN SELECT RAISE(ABORT,'reset metadata rollback'); END");
            else {
                // A deferred constraint fails COMMIT after all reset statements have succeeded.
                connection.Exec("CREATE TABLE reset_parent(id INTEGER PRIMARY KEY); CREATE TABLE reset_child(id INTEGER REFERENCES reset_parent(id) DEFERRABLE INITIALLY DEFERRED);");
                connection.Exec("CREATE TRIGGER fail_reset AFTER UPDATE ON library_meta WHEN NEW.key='identity_metadata' BEGIN INSERT INTO reset_child VALUES(1); END");
            }
            Reject([&] { db.ExecuteIdentity(command(before)); });
            unchanged(db, before, stored, originals);
            if (phase == 2) CHECK(connection.Scalar("SELECT COUNT(*) FROM reset_child") == "0");
            connection.Exec("DROP TRIGGER fail_reset");
            PlayerLibraryDatabase reopened(o); CHECK(reopened.Initialize()->revision == before->revision);
            CHECK(db.ExecuteIdentity(command(before))->entities.empty());
        });
    for (const auto& failure : failures) std::cerr << "reset regression failed: " << failure << '\n';
    CHECK(failures.empty());
}
void OverlapSuggestionRegressions(const fs::path& dir) {
    std::vector<std::string> failures;
    auto run = [&](const char* name, auto action) { try { action(); std::cout << "overlap regression passed: " << name << '\n'; } catch (const std::exception& e) { failures.push_back(std::string(name) + ": " + e.what()); } };
    auto fixture = [&](const char* tag) { return Options{dir / (std::string(tag) + ".db"), dir / (std::string(tag) + ".ini"), dir / (std::string(tag) + ".json"), false}; };
    auto row = [](const char* name, json games, json adventure = json::array()) {
        return json{{"names", {name}}, {"gameIds", games}, {"adventureGroupIds", adventure}};
    };
    auto pair = [](const char* left, const char* right) { return json{{"leftName", left}, {"rightName", right}}; };
    auto suggestion = [](const SnapshotPtr& s, const char* left, const char* right) -> json {
        for (const auto& value : s->identityState["overlapSuggestions"])
            if (value["leftName"] == left && value["rightName"] == right) return value;
        throw std::runtime_error(std::string("Missing suggestion: ") + left + "/" + right);
    };
    auto evidence = [&](const SnapshotPtr& s, const char* left, const char* right, json games, json) {
        const auto value = suggestion(s, left, right);
        CHECK(value.contains("commonGameIds") && !value.contains("commonAdventureIds"));
        auto actualGames = value["commonGameIds"];
        std::sort(actualGames.begin(), actualGames.end()); std::sort(games.begin(), games.end());
        CHECK(actualGames == games);
        CHECK(value["commonIdCount"] == games.size() && !value.contains("commonAdventureIdCount"));
        CHECK(value["commonIdCountIsLowerBound"] == false);
    };
    auto storage = [](const Options& o, bool metadata) {
        Connection connection(o.databasePath); auto result = json::object();
        for (const auto* table : {"player_entities", "player_names", "identifiers", "identifier_spellings", "entity_identifiers"})
            result[table] = connection.Rows(std::string("SELECT * FROM ") + table + " ORDER BY 1,2");
        if (metadata) result["library_meta"] = connection.Rows("SELECT * FROM library_meta ORDER BY key");
        return result;
    };
    run("actual canonical evidence retains display text and job distinctions", [&] {
        PlayerLibraryDatabase db(fixture("overlap-text")); db.Initialize();
        const auto display = ToUtf8(L"Hero \xff03 Job");
        const auto s = db.ImportV2(json::array({
            row("A", {display, "Hero#Job", "Other#Mage", "Case#Job"}, {" Guild#One ", "Guild#One"}),
            row("B", {"Hero#Job", "Other#Priest", "case#Job"}, {"Guild#One", "Guild#Two"}),
            row("C", {"Other#Mage"})}));
        evidence(s, "A", "B", {display}, {"Guild#One"});
        evidence(s, "A", "C", {"Other#Mage"}, json::array());
        CHECK(s->entities.size() == 3);
    });
    run("original game evidence excludes inherited added and deleted IDs", [&] {
        const auto o = fixture("overlap-originals"); SnapshotPtr saved;
        {
            PlayerLibraryDatabase db(o); db.Initialize();
            auto s = db.ImportV2(json::array({
                row("A", {"Keep#Job", "Removed", "OnlyA"}, {"GuildKeep", "GuildRemoved"}),
                row("B", {"Inherited#Mage"}, {"InheritedGuild"}),
                row("C", {"Keep#Job", "Removed", "Inherited#Mage", "NewCommon"}, {"GuildKeep", "GuildRemoved", "InheritedGuild", "NewGuild"}),
                row("D", {"Keep#Other"}, {"GuildKeep"})}));
            s = db.ExecuteIdentity({{"action", "merge"}, {"revision", s->revision}, {"names", {"A", "B"}}});
            s = db.ExecuteIdentity({{"action", "add_alias"}, {"revision", s->revision}, {"sourceName", "A"}, {"newName", "AddedAlias"}});
            evidence(s, "A", "C", {"Keep#Job", "Removed"}, {"GuildKeep", "GuildRemoved"});
            evidence(s, "B", "C", {"Inherited#Mage"}, {"InheritedGuild"});
            const auto before = s->identityMetadata["groups"][0];
            s = db.ExecuteIdentity({{"action", "update_ids"}, {"revision", s->revision}, {"name", "A"}, {"ids", {"Keep#Job", "OnlyA", "Inherited#Mage", "NewCommon"}}});
            CHECK(s->identityMetadata["groups"][0]["beforeMerge"] == before["beforeMerge"]);
            CHECK(!s->identityMetadata["groups"][0].contains("beforeAdventure"));
            evidence(s, "A", "C", {"Keep#Job"}, {"GuildKeep"});
            evidence(s, "B", "C", {"Inherited#Mage"}, {"InheritedGuild"});
            Reject([&] { suggestion(s, "A", "D"); });
            for (const auto& value : s->identityState["overlapSuggestions"])
                CHECK(value["leftName"] != "AddedAlias" && value["rightName"] != "AddedAlias");
            saved = db.LoadSnapshot();
        }
        PlayerLibraryDatabase reopened(o); const auto s = reopened.Initialize();
        CHECK(s->identityState == saved->identityState && s->v2Entities == saved->v2Entities);
    });
    run("capped analysis suggestions display complete lists with exact counts", [&] {
        PlayerLibraryDatabase db(fixture("overlap-capped")); db.Initialize();
        auto rows = json::array();
        for (int i = 0; i < 257; ++i) {
            const auto name = "Player" + std::to_string(i);
            auto games = json::array({"Popular", name});
            if (i < 2) games.push_back("Rare");
            rows.push_back(row(name.c_str(), games, {"PopularGuild"}));
        }
        const auto s = db.ImportV2(rows);
        const auto analysis = AnalyzeIdentityEvidence(*s);
        CHECK(analysis.overlapSuggestions.size() == 1 && analysis.overlapSuggestions[0].commonIdCountIsLowerBound);
        CHECK(analysis.overlapSuggestions[0].commonIdCount == 1);
        CHECK(s->identityState["overlapSuggestionsTruncated"] == true);
        evidence(s, "Player0", "Player1", {"Popular", "Rare"}, {"PopularGuild"});
    });
    run("bulk persists only supplied pairs deduplicates and preserves IDs and payloads", [&] {
        const auto o = fixture("overlap-bulk"); SnapshotPtr saved; json stored;
        {
            PlayerLibraryDatabase db(o); db.Initialize();
            auto s = db.ImportV2(json::array({row("A", {"Shared"}), row("B", {"Shared"}), row("C", {"Shared"}),
                row("D", {"PairDE"}), row("E", {"PairDE"}),
                row("F", {"PairFG"}), row("G", {"PairFG"})}));
            s = db.ExecuteIdentity({{"action", "ignore_overlap"}, {"revision", s->revision}, {"leftName", "D"}, {"rightName", "E"}});
            stored = storage(o, false); const auto before = s;
            s = db.ExecuteIdentity({{"action", "cmd_identity_ignore_overlaps"}, {"revision", s->revision},
                {"pairs", {pair("A", "B"), pair("B", "A"), pair("B", "C"), pair("A", "B"), pair("G", "F")}}});
            CHECK(s->revision == before->revision + 1);
            CHECK(s->legacy == before->legacy && s->v2Entities == before->v2Entities);
            CHECK(s->legacyPayload == before->legacyPayload && s->appendPayload == before->appendPayload);
            CHECK(storage(o, false) == stored);
            const auto ignored = s->identityMetadata["autoSplitFingerprints"];
            CHECK(ignored.size() == 4);
            for (const auto& names : std::vector<std::vector<std::wstring>>{{L"D", L"E"}, {L"A", L"B"}, {L"B", L"C"}, {L"F", L"G"}})
                CHECK(std::count(ignored.begin(), ignored.end(), json(IdentityFingerprint(names))) == 1);
            CHECK(s->identityState["overlapSuggestions"].size() == 1);
            suggestion(s, "A", "C"); saved = s;
        }
        PlayerLibraryDatabase reopened(o); auto s = reopened.Initialize();
        CHECK(s->identityMetadata == saved->identityMetadata && s->revision == saved->revision);
        CHECK(s->identityState == saved->identityState && s->v2Entities == saved->v2Entities && storage(o, false) == stored);
        const auto storedAll = storage(o, true);
        Reject([&] { reopened.ExecuteIdentity({{"action", "ignore_overlaps"}, {"revision", s->revision}, {"pairs", {pair("A", "B"), pair("A", "C")}}}); });
        CHECK(storage(o, true) == storedAll);
        s = reopened.ExecuteIdentity({{"action", "ignore_overlaps"}, {"revision", s->revision}, {"pairs", {pair("C", "A"), pair("A", "C")}}});
        CHECK(s->identityMetadata["autoSplitFingerprints"].size() == 5 && s->identityState["overlapSuggestions"].empty());
    });
    run("bulk empty invalid stale and storage failures roll back entirely", [&] {
        const auto o = fixture("overlap-bulk-rollback"); PlayerLibraryDatabase db(o); db.Initialize();
        auto s = db.ImportV2(json::array({row("A", {"Shared"}), row("B", {"Shared"}), row("C", {"Other"}), row("D", {"Other"})}));
        s = db.ExecuteIdentity({{"action", "merge"}, {"revision", s->revision}, {"names", {"A", "C"}}});
        s = db.LoadSnapshot();
        const auto stored = storage(o, true);
        auto unchanged = [&] {
            const auto after = db.LoadSnapshot();
            CHECK(after->revision == s->revision && after->identityState == s->identityState && after->identityMetadata == s->identityMetadata);
            CHECK(after->legacy == s->legacy && after->v2Entities == s->v2Entities && storage(o, true) == stored);
        };
        const json valid = {{"action", "ignore_overlaps"}, {"revision", s->revision}, {"pairs", {pair("A", "B"), pair("C", "D")}}};
        for (const auto& invalid : std::vector<json>{nullptr, false, "pairs", json::object(), json::array(),
            json::array({pair("A", "B"), nullptr}), json::array({pair("A", "B"), {}}),
            json::array({pair("A", "B"), {{"leftName", 1}, {"rightName", "D"}}}),
            json::array({pair("A", "B"), pair("A", "A")}), json::array({pair("A", "B"), pair("Missing", "D")}),
            json::array({pair("A", "B"), pair("A", "D")}), json::array({pair("A", "B"), pair("A", "C")}),
            json::array({pair("A", "B"), pair("", "D")})}) {
            auto command = valid; command["pairs"] = invalid;
            Reject([&] { db.ExecuteIdentity(command); }); unchanged();
        }
        for (const char* missing : {"pairs", "revision"}) {
            auto command = valid; command.erase(missing); Reject([&] { db.ExecuteIdentity(command); }); unchanged();
        }
        for (const auto& revision : std::vector<json>{s->revision - 1, s->revision + 1, -1, "revision", nullptr, true, 1.5}) {
            auto command = valid; command["revision"] = revision; Reject([&] { db.ExecuteIdentity(command); }); unchanged();
        }
        Connection connection(o.databasePath);
        connection.Exec("CREATE TRIGGER fail_bulk BEFORE UPDATE ON library_meta WHEN NEW.key='identity_metadata' BEGIN SELECT RAISE(ABORT,'bulk rollback'); END");
        Reject([&] { db.ExecuteIdentity(valid); }); unchanged();
        connection.Exec("DROP TRIGGER fail_bulk");
        PlayerLibraryDatabase reopened(o); CHECK(reopened.Initialize()->identityState == s->identityState);
        const auto after = reopened.ExecuteIdentity(valid);
        CHECK(after->revision == s->revision + 1 && after->identityState["overlapSuggestions"].empty());
    });
    for (const auto& failure : failures) std::cerr << "overlap regression failed: " << failure << '\n';
    CHECK(failures.empty());
}
void CreatePlayerRegressions(const fs::path& dir) {
    std::vector<std::string> failures;
    auto run = [&](const char* name, auto action) { try { action(); std::cout << "create player regression passed: " << name << '\n'; } catch (const std::exception& e) { failures.push_back(std::string(name) + ": " + e.what()); } };
    auto fixture = [&](const char* tag) { return Options{dir / (std::string(tag) + ".db"), dir / (std::string(tag) + ".ini"), dir / (std::string(tag) + ".json"), false}; };
    auto command = [](std::uint64_t revision, const char* name, json games, json adventures) {
        return json{{"action", "cmd_identity_create_player"}, {"revision", revision}, {"name", name}, {"ids", games}, {"adventureGroupIds", adventures}};
    };
    auto storage = [](const Options& o) {
        Connection connection(o.databasePath); auto result = json::object();
        for (const char* table : {"player_entities", "player_names", "identifiers", "identifier_spellings", "entity_identifiers", "library_meta"})
            result[table] = connection.Rows(std::string("SELECT * FROM ") + table + " ORDER BY 1,2");
        CHECK(connection.Rows("PRAGMA foreign_key_check").empty());
        return result;
    };
    run("name-only new player survives restart export and lookup", [&] {
        const auto o = fixture("create-name-only"); SnapshotPtr saved;
        auto check = [](const SnapshotPtr& s) {
            CHECK(s->persisted && s->entities.size() == 1);
            const auto* player = s->FindName(L"NewPlayer"); CHECK(player);
            CHECK(player->names == std::vector<std::wstring>{L"NewPlayer"} && !player->cloudId);
            CHECK(player->gameIds.empty());
            CHECK(s->legacy.at(L"NewPlayer").empty() && s->formattedLegacy.at(L"NewPlayer").empty());
            CHECK(s->gameIndex.empty() && s->identifiers.empty());
            CHECK(s->Lookup(L"Guild").candidates.empty() && s->Lookup(L"SecondGuild").candidates.empty());
            CHECK(s->Lookup(L"").candidates.empty());
            CHECK(s->v2Entities == json::array({{{"names", {"NewPlayer"}}, {"gameIds", json::array()}}}));
            CHECK(s->identityState["groups"].empty());
        };
        {
            PlayerLibraryDatabase db(o); const auto before = db.Initialize();
            Reject([&] { db.ExecuteIdentity({{"action", "update_adventure_ids"}, {"revision", before->revision}, {"name", "NewPlayer"}, {"adventureGroupIds", {"Guild"}}}); });
            saved = db.ExecuteIdentity(command(before->revision, " \tNewPlayer\r\n", json::array(), {" Guild ", "Guild", "SecondGuild", " SecondGuild "}));
            CHECK(saved->revision == before->revision + 1 && before->entities.empty()); check(saved);
            CHECK(db.LoadSnapshot()->v2Entities == saved->v2Entities);
        }
        const auto stored = storage(o);
        PlayerLibraryDatabase reopened(o); const auto restarted = reopened.Initialize(); check(restarted);
        CHECK(restarted->revision == saved->revision && storage(o) == stored);
        CHECK(restarted->FindName(L"NewPlayer")->entityId == saved->FindName(L"NewPlayer")->entityId);
        PlayerLibraryDatabase roundtrip(fixture("create-name-roundtrip")); roundtrip.Initialize();
        check(roundtrip.ImportV2({{"entities", restarted->v2Entities}}));
    });
    run("normalization reuses IDs without replacing players and accepts game-only", [&] {
        PlayerLibraryDatabase db(fixture("create-normalized")); db.Initialize();
        auto s = db.ReplaceLegacy({{L"Existing", {L"Hero#Job", L"Shared"}}}); const auto before = s;
        const auto originalId = s->FindName(L"Existing")->entityId;
        const auto gameRef = s->FindName(L"Existing")->gameIds.front();
        s = db.ExecuteIdentity(command(s->revision, "Mixed", {" Hero # Job ", "Hero#Job", ToUtf8(L"Hero\xff03Job"), "hero#Job", "Shared"}, {" Guild ", "Guild", "guild", "Shared"}));
        CHECK(s->revision == before->revision + 1 && s->entities.size() == 2);
        CHECK(s->FindName(L"Existing")->entityId == originalId && s->legacy.at(L"Existing") == before->legacy.at(L"Existing"));
        CHECK(s->FindName(L"Mixed")->entityId != originalId && s->FindName(L"Mixed")->gameIds.size() == 3);
        CHECK(s->FindName(L"Mixed")->gameIds.front() == gameRef);
        CHECK(s->identifiers.size() == 3 && before->identifiers.size() == 2 && !before->FindName(L"Mixed"));
        CHECK((s->FindIdentifier(gameRef)->spellings == std::vector<std::wstring>{L"Hero # Job", L"Hero\xff03Job"}));
        s = db.ExecuteIdentity(command(s->revision, "NameOnlyPeer", json::array(), {"Guild", " Guild "}));
        CHECK(s->entities.size() == 3 && s->identifiers.size() == 3);
        CHECK(s->Lookup(L"Guild").candidates.empty());
        CHECK(s->FindName(L"NameOnlyPeer")->gameIds.empty() && s->FindName(L"NameOnlyPeer")->entityId != s->FindName(L"Mixed")->entityId);
        auto gameOnly = command(s->revision, "GameOnly", {" OnlyGame ", "OnlyGame"}, json::array());
        gameOnly.erase("action"); gameOnly["command"] = "create_player";
        s = db.ExecuteIdentity(gameOnly);
        CHECK(s->entities.size() == 4 && s->legacy.at(L"GameOnly") == std::vector<std::wstring>{L"OnlyGame"});
        CHECK(!s->v2Entities.back().contains("adventureGroupIds"));
        CHECK(s->Lookup(L"OnlyGame").candidates == std::vector<EntityId>{s->FindName(L"GameOnly")->entityId});
        CHECK(db.LoadSnapshot()->v2Entities == s->v2Entities);
    });
    run("invalid empty existing and stale requests leave all rows unchanged", [&] {
        const auto o = fixture("create-invalid"); PlayerLibraryDatabase db(o); db.Initialize();
        db.ImportV2(json::array({{{"entityId", "CloudExisting"}, {"names", {"Existing", "Alias"}}, {"gameIds", {"Retained"}}, {"adventureGroupIds", {"SavedGuild"}}}}));
        const auto before = db.LoadSnapshot(); const auto stored = storage(o);
        auto unchanged = [&] {
            const auto after = db.LoadSnapshot();
            CHECK(after->revision == before->revision && after->v2Entities == before->v2Entities);
            CHECK(after->identityMetadata == before->identityMetadata && after->identityState == before->identityState);
            CHECK(after->legacy == before->legacy && storage(o) == stored);
        };
        auto reject = [&](const json& value) { Reject([&] { db.ExecuteIdentity(value); }); unchanged(); };
        const auto valid = command(before->revision, "NewPlayer", {"FreshGame"}, {"FreshGuild"});
        for (const auto& invalid : std::vector<json>{nullptr, true, 42, "command", json::array(), json::object()}) reject(invalid);
        for (const char* missing : {"name", "ids", "revision"}) {
            auto value = valid; value.erase(missing); reject(value);
        }
        for (const auto& name : std::vector<json>{nullptr, 42, true, json::array(), json::object(), "", " \t\r\n ",
            "A=B", "A\nB", "A\x7f" "B", std::string("A\0B", 3), std::string(257, 'n'), std::string("\xc3\x28"),
            "Existing", " Existing ", " Alias "}) {
            auto value = valid; value["name"] = name; reject(value);
        }
        for (const char* field : {"ids"}) {
            for (const auto& ids : std::vector<json>{nullptr, true, 42, "(NotAnArray)", json::object(),
                json::array({"Fresh", nullptr}), json::array({"Fresh", 1}), json::array({"Fresh", true}),
                json::array({"Fresh", json::object()}), json::array({"Fresh", json::array()}),
                json::array({"Fresh", ""}), json::array({"Fresh", " \t "}), json::array({"Fresh", "A\nB"}),
                json::array({"Fresh", "A\x7f" "B"}), json::array({"Fresh", std::string("A\0B", 3)}),
                json::array({"Fresh", std::string(1025, 'i')}), json::array({"Fresh", std::string("\xc3\x28")}),
                json(std::vector<std::string>(100001, "Fresh"))}) {
                auto value = valid; value[field] = ids; reject(value);
            }
        }
        for (const auto& revision : std::vector<json>{before->revision - 1, before->revision + 1, -1, static_cast<std::uint64_t>(-1), "revision", nullptr, true, 1.5}) {
            auto value = valid; value["revision"] = revision; reject(value);
        }
        PlayerLibraryDatabase reopened(o); CHECK(reopened.Initialize()->v2Entities == before->v2Entities);
        unchanged();
        const auto after = db.ExecuteIdentity(valid);
        CHECK(after->revision == before->revision + 1 && after->entities.size() == before->entities.size() + 1);
        CHECK(after->FindName(L"Existing")->cloudId == "CloudExisting");
        CHECK(after->FindName(L"Existing")->entityId == before->FindName(L"Existing")->entityId);
    });
    run("write and commit failures preserve database and published snapshot", [&] {
        for (bool commitFailure : {false, true}) {
            const auto o = fixture(commitFailure ? "create-failed-commit" : "create-failed-write");
            PlayerLibraryStore store(o); auto result = Wait(store, store.Initialize()); CHECK(result.ok);
            result = Wait(store, store.SubmitLegacy({{L"Existing", {L"Retained"}}})); CHECK(result.ok);
            const auto before = store.GetSnapshot(); const auto stored = storage(o);
            const auto request = command(before->revision, "Attempted", {"NewGame", " NewGame ", "SecondGame"}, json::array());
            Connection connection(o.databasePath);
            if (commitFailure) connection.Exec("CREATE TABLE commit_parent(id INTEGER PRIMARY KEY); CREATE TABLE commit_fault(value INTEGER REFERENCES commit_parent(id) DEFERRABLE INITIALLY DEFERRED); CREATE TRIGGER fail_create AFTER INSERT ON player_names WHEN NEW.name='Attempted' BEGIN INSERT INTO commit_fault VALUES(99); END;");
            else connection.Exec("CREATE TRIGGER fail_create BEFORE UPDATE ON library_meta WHEN NEW.key='identity_metadata' BEGIN SELECT RAISE(ABORT,'injected create write failure'); END");
            result = Wait(store, store.ExecuteIdentity(request)); CHECK(!result.ok);
            if (result.error.find(commitFailure ? "FOREIGN KEY constraint failed" : "injected create write failure") == std::string::npos)
                throw std::runtime_error("Unexpected create failure: " + result.error);
            CHECK(store.GetSnapshot() == before && result.snapshot == before && storage(o) == stored);
            CHECK(!before->FindName(L"Attempted") && before->Lookup(L"NewGame").candidates.empty());
            if (commitFailure) CHECK(connection.Scalar("SELECT count(*) FROM commit_fault") == "0");
            connection.Exec("DROP TRIGGER fail_create");
            if (commitFailure) connection.Exec("DROP TABLE commit_fault; DROP TABLE commit_parent;");
            {
                PlayerLibraryDatabase reopened(o); const auto loaded = reopened.Initialize();
                CHECK(loaded->revision == before->revision && loaded->v2Entities == before->v2Entities && storage(o) == stored);
            }
            result = Wait(store, store.ExecuteIdentity(request)); CHECK(result.ok && result.command == request);
            const auto after = store.GetSnapshot(); CHECK(after == result.snapshot);
            CHECK(after->revision == before->revision + 1 && after->entities.size() == before->entities.size() + 1);
            CHECK(after->identifiers.size() == before->identifiers.size() + 2);
            CHECK(after->FindName(L"Attempted")->gameIds.size() == 2);
            Stop(store);
            PlayerLibraryDatabase reopened(o); CHECK(reopened.Initialize()->v2Entities == after->v2Entities);
        }
    });
    for (const auto& failure : failures) std::cerr << "create player regression failed: " << failure << '\n';
    CHECK(failures.empty());
}
void GameOnlyStorageMigrationRegressions(const fs::path& dir) {
    auto fixture = [&](const std::string& tag, bool automatic) {
        Options o{dir / (tag + ".db"), dir / (tag + ".ini"), dir / (tag + ".json"), false};
        Connection c(o.databasePath);
        c.Exec(R"sql(
PRAGMA foreign_keys=ON;
CREATE TABLE library_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);
CREATE TABLE player_entities(entity_id INTEGER PRIMARY KEY,cloud_id TEXT,deleted INTEGER NOT NULL DEFAULT 0);
CREATE UNIQUE INDEX active_cloud_id ON player_entities(cloud_id) WHERE deleted=0 AND cloud_id IS NOT NULL;
CREATE TABLE player_names(name TEXT PRIMARY KEY COLLATE BINARY,entity_id INTEGER NOT NULL REFERENCES player_entities(entity_id));
CREATE INDEX names_entity ON player_names(entity_id);
CREATE TABLE identifiers(identifier_id INTEGER PRIMARY KEY,kind INTEGER NOT NULL CHECK(kind IN(0,1)),canonical_key TEXT NOT NULL COLLATE BINARY,display_text TEXT NOT NULL,UNIQUE(kind,canonical_key));
CREATE TABLE identifier_spellings(identifier_id INTEGER NOT NULL REFERENCES identifiers(identifier_id),raw_text TEXT NOT NULL,PRIMARY KEY(identifier_id,raw_text));
CREATE TABLE entity_identifiers(entity_id INTEGER NOT NULL REFERENCES player_entities(entity_id),identifier_id INTEGER NOT NULL REFERENCES identifiers(identifier_id),position INTEGER NOT NULL,PRIMARY KEY(entity_id,identifier_id));
CREATE INDEX identifier_owners ON entity_identifiers(identifier_id);
INSERT INTO library_meta VALUES('storage_version','2'),('normalizer_version','1'),('revision','8'),('identity_metadata','{}'),('remote_revisions','{"public":17}'),('migration_fingerprint','keep-audit'),('adventure_metadata','retired');
INSERT INTO player_entities VALUES(1,'CloudA',0),(2,'CloudB',1),(3,'CloudNameOnly',0),(4,'CloudShared',0);
INSERT INTO player_names VALUES('adventureGroupIds',1),('beforeAdventure',1),('NameOnly',3),('Shared',4);
INSERT INTO identifiers VALUES(10,0,'Role' || char(31) || 'Job','Role#Job'),(20,0,'Second' || char(31) || 'Mage','Second#Mage'),(11,1,'Role#Job','Role#Job'),(12,1,'RetiredOnly','RetiredOnly');
INSERT INTO identifier_spellings VALUES(10,'Role # Job'),(11,' Role#Job '),(12,'Retired spelling');
INSERT INTO entity_identifiers VALUES(1,10,0),(1,20,1),(1,11,2),(3,12,0),(4,10,0);
)sql");
        const json metadata = {
            {"autoGroupPolicyVersion", 4}, {"autoSplitFingerprints", {"keep-ignore"}},
            {"autoSplitNameSets", json::array({json::array({"NameOnly", "Shared"})})}, {"adventureGroupIds", {11, 12}},
            {"groups", json::array({{
                {"groupId", "historical"}, {"source", automatic ? "automatic" : "manual"},
                {"names", {"adventureGroupIds", "beforeAdventure"}},
                {"beforeMerge", {{"adventureGroupIds", {10, 11}}, {"beforeAdventure", {20}}}},
                {"beforeAdventure", {{"adventureGroupIds", {11}}, {"beforeAdventure", {12}}}},
                {"beforeEntities", {{"adventureGroupIds", {{"entityId", 1}, {"cloudId", "CloudA"}}},
                    {"beforeAdventure", {{"entityId", 2}, {"cloudId", "CloudB"}}}}}
            }})}
        };
        c.SetMetadata(metadata);
        return o;
    };
    auto storage = [](const Options& o) {
        Connection c(o.databasePath); json rows;
        for (const auto* table : {"sqlite_master", "library_meta", "player_entities", "player_names", "identifiers", "identifier_spellings", "entity_identifiers"})
            rows[table] = c.Rows(std::string("SELECT * FROM ") + table + " ORDER BY 1,2");
        CHECK(c.Rows("PRAGMA foreign_key_check").empty());
        return rows;
    };
    for (bool automatic : {false, true}) {
        const auto o = fixture(automatic ? "storage-v3-automatic" : "storage-v3-manual", automatic);
        SnapshotPtr saved;
        {
            PlayerLibraryDatabase db(o); auto s = db.Initialize();
            CHECK(s->revision == 9 && s->identityMetadata["autoGroupPolicyVersion"] == 5);
            CHECK(s->entities.size() == (automatic ? 4u : 3u));
            CHECK(s->identifiers.size() == 2 && s->FindName(L"NameOnly")->gameIds.empty());
            CHECK(s->FindName(L"NameOnly")->cloudId == "CloudNameOnly");
            CHECK(s->Lookup(L"RetiredOnly").candidates.empty());
            CHECK(s->Lookup(L"Role#Job").candidates.size() == 2 && s->Lookup(L"Role#Job").conflict);
            CHECK(s->identityMetadata["autoSplitFingerprints"] == json::array({"keep-ignore"}));
            CHECK(s->identityMetadata["autoSplitNameSets"] == json::array({json::array({"NameOnly", "Shared"})}));
            CHECK(!s->identityMetadata.contains("adventureGroupIds"));
            CHECK(s->FindName(L"adventureGroupIds") && s->FindName(L"beforeAdventure"));
            if (!automatic) {
                CHECK(s->FindName(L"adventureGroupIds")->entityId == s->FindName(L"beforeAdventure")->entityId);
                const auto& group = s->identityMetadata["groups"][0];
                CHECK(group["source"] == "manual" && !group.contains("beforeAdventure"));
                CHECK(group["beforeMerge"]["adventureGroupIds"] == json::array({"Role#Job"}));
                CHECK(group["beforeMerge"]["beforeAdventure"] == json::array({"Second#Mage"}));
                CHECK(group["beforeEntities"].contains("adventureGroupIds") && group["beforeEntities"].contains("beforeAdventure"));
            } else {
                CHECK(s->identityMetadata["groups"].empty());
                CHECK(s->FindName(L"adventureGroupIds")->entityId != s->FindName(L"beforeAdventure")->entityId);
                CHECK(s->FindName(L"beforeAdventure")->cloudId == "CloudB");
            }
            for (const auto& row : s->v2Entities) CHECK(!row.contains("adventureGroupIds"));
            for (const auto& row : s->identityState["entries"]) CHECK(!row.contains("adventureGroupIds"));
            CHECK(ParseLegacy(SerializeLegacy(s->legacy)).at(L"NameOnly").empty());
            const auto oldPayload = json::array({{{"names", {"NameOnly"}}, {"gameIds", json::array()},
                {"adventureGroupIds", {{"malformed", {12, "RetiredOnly"}}}}}});
            CHECK(db.ImportV2(oldPayload)->revision == s->revision);
            saved = db.LoadSnapshot();
        }
        const auto stored = storage(o);
        {
            PlayerLibraryDatabase db(o); const auto s = db.Initialize();
            CHECK(s->revision == saved->revision && s->identityMetadata == saved->identityMetadata);
            CHECK(s->v2Entities == saved->v2Entities && storage(o) == stored);
            Connection c(o.databasePath);
            CHECK(c.Scalar("SELECT value FROM library_meta WHERE key='storage_version'") == "3");
            CHECK(c.Scalar("SELECT value FROM library_meta WHERE key='remote_revisions'") == "{\"public\":17}");
            CHECK(c.Scalar("SELECT value FROM library_meta WHERE key='migration_fingerprint'") == "keep-audit");
            CHECK(c.Scalar("SELECT count(*) FROM library_meta WHERE key='adventure_metadata'") == "0");
            CHECK(c.Scalar("SELECT count(*) FROM identifiers WHERE kind<>0") == "0");
            CHECK(c.Scalar("SELECT count(*) FROM identifier_spellings") == "1");
            CHECK(sqlite3_exec(c.db, "INSERT INTO identifiers VALUES(99,1,'bad','bad')", nullptr, nullptr, nullptr) == SQLITE_CONSTRAINT);
            if (!automatic) {
                const auto split = db.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", "historical"}, {"splitAll", true}});
                CHECK(split->entities.size() == 4 && split->FindName(L"beforeAdventure")->cloudId == "CloudB");
                CHECK(split->legacy.at(L"adventureGroupIds") == std::vector<std::wstring>{L"Role#Job"});
                CHECK(split->legacy.at(L"beforeAdventure") == std::vector<std::wstring>{L"Second#Mage"});
            }
        }
    }
    for (bool commitFailure : {false, true}) {
        const auto o = fixture(commitFailure ? "storage-v3-commit-rollback" : "storage-v3-write-rollback", false);
        Connection c(o.databasePath);
        if (commitFailure) c.Exec("CREATE TABLE fault_parent(id INTEGER PRIMARY KEY); CREATE TABLE fault_child(id INTEGER REFERENCES fault_parent(id) DEFERRABLE INITIALLY DEFERRED); CREATE TRIGGER fail_migration AFTER UPDATE ON library_meta WHEN NEW.key='storage_version' BEGIN INSERT INTO fault_child VALUES(99); END;");
        else c.Exec("CREATE TRIGGER fail_migration BEFORE UPDATE ON library_meta WHEN NEW.key='storage_version' BEGIN SELECT RAISE(ABORT,'migration write failure'); END;");
        const auto before = storage(o);
        { PlayerLibraryDatabase db(o); Reject([&] { db.Initialize(); }); }
        CHECK(storage(o) == before);
        CHECK(c.Scalar("SELECT value FROM library_meta WHERE key='storage_version'") == "2");
        CHECK(c.Scalar("SELECT count(*) FROM identifiers WHERE kind=1") == "2");
        if (commitFailure) CHECK(c.Scalar("SELECT count(*) FROM fault_child") == "0");
        c.Exec("DROP TRIGGER fail_migration");
        PlayerLibraryDatabase retry(o); CHECK(retry.Initialize()->revision == 9);
    }
    std::cout << "Storage version 3 / policy version 5 migration: manual preservation, automatic reanalysis, named metadata keys, rollback and idempotency passed.\n";
}
void GameOnlyImportRegressions(const fs::path& dir) {
    Options options{dir / "game-only.db", dir / "game-only.ini", dir / "game-only.json", false};
    PlayerLibraryDatabase db(options); db.Initialize();
    auto s = db.ImportV2(json::array({
        {{"names", {"NameOnly"}}, {"gameIds", json::array()}, {"adventureGroupIds", {"Guild1", "Guild2", "Guild3"}}},
        {{"names", {"Peer"}}, {"gameIds", {"Hero#Job"}}, {"adventureGroupIds", {"Guild1", "Guild2", "Guild3"}}}
    }));
    CHECK(s->entities.size() == 2 && s->identifiers.size() == 1);
    CHECK(s->FindName(L"NameOnly") && s->FindName(L"NameOnly")->gameIds.empty());
    CHECK(s->Lookup(L"Guild1").candidates.empty());
    CHECK(s->v2Entities.dump().find("adventure") == std::string::npos);
    CHECK(s->identityState.dump().find("Adventure") == std::string::npos);
    CHECK(s->identityState.dump().find("adventure") == std::string::npos);
    const auto revision = s->revision;
    Reject([&] { db.ExecuteIdentity({{"action", "update_adventure_ids"}, {"revision", revision}, {"name", "Peer"}, {"ids", {"BadGuild"}}}); });
    CHECK(db.LoadSnapshot()->revision == revision);
    std::cout << "Game-only import, name-only preservation and removed command regressions passed.\n";
}
int main(int argc, char** argv) {
    try {
        CHECK(argc == 2);
        fs::path dir = argv[1]; fs::create_directories(dir);
        GameOnlyStorageMigrationRegressions(dir);
        GameOnlyImportRegressions(dir);
        CreatePlayerRegressions(dir);
        OverlapSuggestionRegressions(dir);
        ResetRegressions(dir);
        DenseThresholdRegressions(dir);
        RemovedIdentityEvidenceRegressions(dir);
        ThresholdRegressions(dir);
        ThresholdMigrationRegressions(dir);
        MatchIdentityRegressions(dir);
        ReviewRegressions(dir);
#ifdef DNF_PLAYER_LIBRARY_TESTING
        ValidateCloudNameForTesting(L"Player_123", true);
        ValidateCloudNameForTesting(L"\u9009\u624b", true);
        ValidateCloudNameForTesting(std::wstring(64, L'A'), true);
        Reject([&] { ValidateCloudNameForTesting(std::wstring(65, L'A'), true); });
        ValidateCloudNameForTesting(L"e\u0301", true);
        Reject([&] { ValidateCloudNameForTesting(L"\u0301", true); });
        Reject([&] { ValidateCloudNameForTesting(L"\u00ad", true); });
        Reject([&] { ValidateCloudNameForTesting(L"A\u200f", true); });
        Reject([&] { ValidateCloudNameForTesting(L"A\u2028", true); });
        Reject([&] { ValidateCloudNameForTesting(L"A\u2066", true); });
        Reject([&] { ValidateCloudNameForTesting(L"A\u0085", true); });
        Reject([&] { ValidateCloudNameForTesting(std::wstring(1, wchar_t(0xd800)), true); });
        Reject([&] { ValidateCloudNameForTesting(L"\U0001f600", true); });
        const auto complexName = L"A" + std::wstring(64, L'\u0301');
        Reject([&] { ValidateCloudNameForTesting(complexName, true); });
        if (HasSystemIcuForTesting()) {
            ValidateCloudNameForTesting(complexName, false);
            ValidateCloudNameForTesting(L"\U0001f600", false);
            Reject([&] { ValidateCloudNameForTesting(L"A\u200f", false); });
            Reject([&] { ValidateCloudNameForTesting(std::wstring(65, L'A'), false); });
        }
        std::cout << "Cloud name validation: forced fallback passed; system ICU=" << (HasSystemIcuForTesting() ? "available" : "unavailable") << '\n';
#endif
        CHECK(CanonicalKey(L" Hero \xff03 Job ", IdentifierKind::Game) == CanonicalKey(L"Hero#Job", IdentifierKind::Game));
        CHECK(CanonicalKey(L"Hero#Job", IdentifierKind::Game) != CanonicalKey(L"hero#Job", IdentifierKind::Game));
        CHECK(CanonicalKey(L"Hero#Job", IdentifierKind::Game) != CanonicalKey(L"Hero#Other", IdentifierKind::Game));
        CHECK(CanonicalKey(L"Hero# ", IdentifierKind::Game) == CanonicalKey(L"Hero", IdentifierKind::Game));
        CHECK(ParseIds(L"(Hero#Job)( Other )") == std::vector<std::wstring>({L"Hero#Job", L"Other"}));
        CHECK(IdentityFingerprint({L"Beta", L"Alpha"}) == IdentityFingerprint({L"Alpha", L"Beta"}));
        CHECK(IdentityFingerprint({L"Alpha", L"Beta"}) == "B97B32ECEE5B46A9");
        Options o{dir / "library.db", dir / "alias_db.ini", dir / "groups.json", true};
        const std::string ini = "Alpha=(Hero#Job)(Shared)\r\nBeta=(Hero#Job)(Other)\r\nCase=(hero#Job)\r\nEmpty=\r\n";
        Write(o.legacyPath, ini);
        json metadata = {{"version", 1}, {"autoSplitFingerprints", json::array()}, {"groups", json::array()}};
        Write(o.identityGroupsPath, metadata.dump());
        SnapshotPtr saved;
        {
            PlayerLibraryDatabase db(o);
            auto s = db.Initialize();
            CHECK(s->persisted); CHECK(s->legacy.size() == 4); CHECK(s->identifiers.size() == 4);
            CHECK(s->Lookup(L"Hero#Job").candidates.size() == 2);
            CHECK(s->Lookup(L"hero#Job").candidates.size() == 1);
            auto migratedMetadata = metadata; migratedMetadata["autoGroupPolicyVersion"] = 5;
            CHECK(s->identityMetadata == migratedMetadata);
            CHECK(ParseLegacy(SerializeLegacy(s->legacy)) == s->legacy);
            auto before = s;
            Reject([&] { db.ReplaceLegacy({{L"", {L"bad"}}}, metadata); });
            CHECK(db.LoadSnapshot()->revision == before->revision);
            Reject([&] { db.ImportV2(json::array({{{"entityId", "remote:arbitrary/id"}, {"names", {"Alpha"}}}})); });
            s = db.ImportV2(json::array({{{"entityId", "remote_abc-123"}, {"names", {"Alpha"}}, {"gameIds", {"Hero#Job"}}, {"adventureGroupIds", {"Guild"}}}}));
            CHECK(s->FindName(L"Alpha")->cloudId == "remote_abc-123");
            CHECK(s->Lookup(L"Guild").candidates.empty());
            CHECK(s->Lookup(L"hero#Job").candidates.front() == s->FindName(L"Case")->entityId);
            auto alphaId = s->FindName(L"Alpha")->entityId;
            s = db.ReplaceLegacy(s->legacy, s->identityMetadata);
            CHECK(s->FindName(L"Alpha")->entityId == alphaId);
            Reject([&] { db.ImportV2(json::array({{{"names", {"Bad"}}, {"gameIds", {17}}}})); });
            CHECK(db.LoadSnapshot()->FindName(L"Bad") == nullptr);
            s = db.ExecuteIdentity({{"action", "cmd_identity_merge"}, {"revision", s->revision}, {"names", {"Alpha", "Beta"}}});
            CHECK(s->FindName(L"Alpha")->entityId == s->FindName(L"Beta")->entityId);
            CHECK(s->legacy.at(L"Alpha").size() == 3);
            std::string group = s->identityState["groups"][0]["groupId"];
            auto rev = s->revision;
            Reject([&] { db.ExecuteIdentity({{"action", "update_ids"}, {"revision", rev - 1}, {"groupId", group}, {"ids", {"oops"}}}); });
            auto edited = s->legacy; edited[L"Alpha"] = {L"Replacement"};
            s = db.ReplaceLegacy(edited, s->identityMetadata);
            CHECK(s->legacy.at(L"Alpha") == s->legacy.at(L"Beta"));
            CHECK(s->legacy.at(L"Alpha") == std::vector<std::wstring>{L"Replacement"});
            Reject([&] { db.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", group}, {"splitAll", true}}); });
            s = db.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", group}, {"splitAll", true}, {"keepNewIdsWith", "Alpha"}});
            CHECK(s->FindName(L"Alpha")->entityId != s->FindName(L"Beta")->entityId);
            CHECK(s->legacy.at(L"Beta").size() == 2);
            s = db.ExecuteIdentity({{"action", "ignore_overlap"}, {"revision", s->revision}, {"leftName", "Alpha"}, {"rightName", "Beta"}});
            CHECK(!s->identityMetadata["autoSplitFingerprints"].empty());
            {
                Connection connection(o.databasePath);
                auto encoded = json::parse(connection.Scalar("SELECT value FROM library_meta WHERE key='identity_metadata'"));
                CHECK(encoded["autoSplitFingerprints"] == s->identityMetadata["autoSplitFingerprints"]);
                connection.Exec("CREATE TRIGGER fail_name BEFORE INSERT ON player_names BEGIN SELECT RAISE(ABORT,'injected disk write failure'); END");
                auto proposed = s->legacy; proposed[L"Attempted"] = {L"not persisted"};
                Reject([&] { db.ReplaceLegacy(proposed); });
                CHECK(db.LoadSnapshot()->legacy == s->legacy);
                CHECK(db.LoadSnapshot()->revision == s->revision);
                CHECK(proposed.count(L"Attempted") == 1);
                connection.Exec("DROP TRIGGER fail_name");
                connection.Exec("CREATE TABLE commit_parent(id INTEGER PRIMARY KEY); CREATE TABLE commit_fault(value INTEGER REFERENCES commit_parent(id) DEFERRABLE INITIALLY DEFERRED); CREATE TRIGGER fail_commit AFTER INSERT ON player_names BEGIN INSERT INTO commit_fault VALUES(99); END;");
                Reject([&] { db.ReplaceLegacy(proposed); });
                CHECK(db.LoadSnapshot()->legacy == s->legacy);
                CHECK(db.LoadSnapshot()->revision == s->revision);
                CHECK(connection.Scalar("SELECT count(*) FROM commit_fault") == "0");
                connection.Exec("DROP TRIGGER fail_commit; DROP TABLE commit_fault; DROP TABLE commit_parent;");
                CHECK(connection.Scalar("SELECT count(*) FROM identifiers") == std::to_string(s->identifiers.size()));
            }
            saved = s;
        }
        Write(o.legacyPath, "Bogus=(DoNotImport)\n");
        {
            PlayerLibraryDatabase db(o); auto s = db.Initialize();
            CHECK(s->revision == saved->revision); CHECK(s->legacy == saved->legacy);
            CHECK(s->identityMetadata == saved->identityMetadata);
        }
        bool backup = false;
        for (const auto& p : fs::directory_iterator(dir)) if (p.path().filename().string().find(".migrated.bak") != std::string::npos) backup = true;
        CHECK(backup);
        {
            PlayerLibraryStore store(o);
            const auto start = std::chrono::steady_clock::now();
            auto id = store.Initialize();
            CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100));
            auto r = Wait(store, id); CHECK(r.ok); CHECK(r.snapshot->persisted);
            auto old = store.GetSnapshot();
            r = Wait(store, store.SubmitLegacy({{L"", {L"Invalid"}}})); CHECK(!r.ok);
            CHECK(store.GetSnapshot()->revision == old->revision);
            store.Shutdown();
            auto rejected = Wait(store, store.RequestRefresh()); CHECK(!rejected.ok);
            Stop(store);
        }
        {
            Write(o.legacyPath, "Bogus=(DoNotImport)\n");
            Options bad{dir, o.legacyPath, o.identityGroupsPath, true};
            PlayerLibraryStore store(bad); auto r = Wait(store, store.Initialize());
            CHECK(!r.ok); CHECK(r.snapshot); CHECK(!r.snapshot->persisted);
            CHECK(r.snapshot->revision > 0);
            CHECK(r.snapshot->FindName(L"Bogus"));
            Stop(store);
        }
        {
            Options cloud{dir / "cloud.db", dir / "cloud.ini", dir / "cloud-groups.json", false};
            PlayerLibraryDatabase db(cloud); auto s = db.Initialize();
            s = db.ImportV2({{"ok", true}, {"data", {{"players", {{"One", "(Game)(Second)"}, {"Two", {"Case", "case"}}}}}}});
            CHECK(s->legacy.size() == 2); CHECK(s->legacy.at(L"Two").size() == 2);
            s = db.ImportV2({{"players", {{"One", json::array()}, {"Three", "(New)"}}}});
            CHECK(s->legacy.at(L"One").size() == 2); CHECK(s->legacy.size() == 3);
            s = db.ImportV2({{"entities", json::array({{{"entityId", "remote_shared"}, {"names", {"CloudA", "CloudB"}}, {"gameIds", {"CloudGame"}}, {"adventureGroupIds", {"CloudGuild"}}}})}});
            CHECK(s->FindName(L"CloudA")->entityId == s->FindName(L"CloudB")->entityId);
            CHECK(s->FindName(L"CloudA")->cloudId == "remote_shared");
            auto group = s->identityState["groups"][0];
            CHECK(!group.contains("adventureGroupIds"));
            auto frozen = s;
            s = db.ExecuteIdentity({{"action", "cmd_identity_add_alias"}, {"revision", s->revision}, {"sourceName", "Three"}, {"newName", "FreshAlias"}});
            CHECK(s->FindName(L"Three")->entityId == s->FindName(L"FreshAlias")->entityId);
            CHECK(!frozen->FindName(L"FreshAlias"));
            std::string aliasGroup;
            for (const auto& g : s->identityState["groups"]) if (std::find(g["names"].begin(), g["names"].end(), json("FreshAlias")) != g["names"].end()) aliasGroup = g["groupId"];
            for (const auto& g : s->identityState["groups"]) if (std::find(g["names"].begin(), g["names"].end(), json("FreshAlias")) != g["names"].end()) aliasGroup = g["groupId"];
            s = db.ExecuteIdentity({{"action", "delete_alias"}, {"revision", s->revision}, {"groupId", aliasGroup}, {"name", "FreshAlias"}});
            CHECK(!s->FindName(L"FreshAlias")); CHECK(s->FindName(L"Three"));
            auto cloudGroup = group["groupId"].get<std::string>();
            s = db.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", cloudGroup}, {"splitAll", true}});
            CHECK(s->FindName(L"CloudA")->entityId != s->FindName(L"CloudB")->entityId);
            auto command = json{{"action", "refresh"}}; CHECK(db.ExecuteIdentity(command)->revision == s->revision);
            Reject([&] { db.ImportV2({{"entities", json::array({{{"names", {std::string(65, 'a')}}, {"gameIds", json::array()}, {"adventureGroupIds", json::array()}}})}}); });
            Reject([&] { db.ImportV2({{"entities", json::array({{{"names", {"Valid"}}, {"gameIds", {std::string(129, 'x')}}}})}}); });
            Reject([&] { db.ImportV2({{"players", {{"Valid", {123}}}}}); });
            Reject([&] { db.ImportV2({{"entities", json::array({{{"names", {"Valid"}}, {"gameIds", "(bad array)"}}})}}); });
            CHECK(db.LoadSnapshot()->revision == s->revision);
            auto v2 = s->v2Entities.dump(); CHECK(v2.find("beforeMerge") == std::string::npos); CHECK(v2.find("autoSplitFingerprints") == std::string::npos);
        }
        {
            Options automatic{dir / "auto.db", dir / "auto.ini", dir / "auto-groups.json", false};
            PlayerLibraryDatabase db(automatic); db.Initialize();
            auto s = db.ReplaceLegacy({{L"First", {L"Same", L"Two", L"Three", L"Four", L"Five"}}, {L"Second", {L"Same", L"Two", L"Three", L"Four", L"Five"}}});
            CHECK(s->identityState["groups"][0]["source"] == "automatic");
            auto changed = s->legacy; changed[L"First"] = {L"Edited"};
            s = db.ReplaceLegacy(changed, s->identityMetadata);
            CHECK(s->legacy.at(L"Second") == std::vector<std::wstring>{L"Edited"});
            CHECK(s->FindName(L"First")->entityId == s->FindName(L"Second")->entityId);
            Connection connection(automatic.databasePath);
            auto encoded = json::parse(connection.Scalar("SELECT value FROM library_meta WHERE key='identity_metadata'"));
            CHECK(encoded["groups"][0]["beforeMerge"]["First"][0].is_number_integer());
        }
        {
            Options corrupt{dir / "corrupt-metadata.db", dir / "good.ini", dir / "bad.json", false};
            Write(corrupt.legacyPath, "Retained=(Safe)\n"); Write(corrupt.identityGroupsPath, "{broken");
            PlayerLibraryDatabase db(corrupt); auto s = db.Initialize();
            CHECK(s->persisted && s->FindName(L"Retained")); CHECK(!db.GetWarning().empty());
            CHECK(fs::file_size(corrupt.identityGroupsPath) == 7);
        }
        {
            Options ignored{dir / "ignored.db", dir / "ignored.ini", dir / "ignored.json", false};
            PlayerLibraryDatabase db(ignored); db.Initialize();
            auto s = db.ReplaceLegacy({{L"A", {L"same"}}, {L"B", {L"same"}}});
            s = db.ExecuteIdentity({{"action", "ignore_overlap"}, {"revision", s->revision}, {"leftName", "A"}, {"rightName", "B"}});
            CHECK(s->identityState["groups"].empty());
            s = db.ImportV2({{"players", {{"C", {"same"}}}}});
            CHECK(s->identityState["groups"].empty());
            CHECK(db.LoadSnapshot()->identityState["groups"].empty());
        }
        {
            Options spelling{dir / "spellings.db", dir / "spellings.ini", dir / "spellings.json", false};
            PlayerLibraryDatabase db(spelling); db.Initialize();
            auto s = db.ReplaceLegacy({{L"Upper", {L"Hero#Job", L"Hero\xff03Job"}}, {L"Lower", {L"hero#Job"}}});
            CHECK(s->identifiers.size() == 2); CHECK(s->legacy.at(L"Upper").size() == 1);
            const auto* id = s->FindIdentifier(s->FindName(L"Upper")->gameIds.front());
            CHECK(id->spellings == std::vector<std::wstring>{L"Hero\xff03Job"});
            s = db.LoadSnapshot(); id = s->FindIdentifier(s->FindName(L"Upper")->gameIds.front());
            CHECK(id->spellings == std::vector<std::wstring>{L"Hero\xff03Job"});
        }
        {
            Options blocked{dir / "blocked.db", dir / "blocked.ini", dir / "blocked.json", true};
            PlayerLibraryStore store(blocked); auto r = Wait(store, store.Initialize()); CHECK(r.ok);
            auto baseline = store.GetSnapshot();
            Connection connection(blocked.databasePath); connection.Exec("BEGIN IMMEDIATE");
            auto started = std::chrono::steady_clock::now();
            auto request = store.SubmitLegacy({{L"Queued", {L"Value"}}});
            CHECK(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(100));
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            auto pending = store.RequestRefresh();
            started = std::chrono::steady_clock::now(); store.Shutdown();
            CHECK(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(100));
            connection.Exec("ROLLBACK"); Stop(store);
            bool acknowledged = false, cancelled = false;
            for (const auto& result : store.Drain()) { if (result.requestId == request) acknowledged = true; if (result.requestId == pending) cancelled = !result.ok; }
            CHECK(acknowledged && cancelled); CHECK(baseline->legacy.empty());
        }
        {
            Options exported{dir / "export.db", dir / "export.ini", dir / "export.json", true};
            { PlayerLibraryDatabase db(exported); db.Initialize(); }
            fs::create_directories(exported.legacyPath);
            PlayerLibraryStore store(exported); auto result = Wait(store, store.Initialize());
            CHECK(result.ok && result.snapshot->persisted);
            CHECK(result.message.find("export failed") != std::string::npos);
            result = Wait(store, store.SubmitLegacy({{L"StillCommitted", {L"ID"}}}));
            CHECK(result.ok && result.snapshot->FindName(L"StillCommitted")); Stop(store);
            PlayerLibraryDatabase db(exported); CHECK(db.Initialize()->FindName(L"StillCommitted"));
        }
        {
            Options reset{dir / "reset.db", dir / "reset.ini", dir / "reset.json", true};
            PlayerLibraryStore store(reset); auto r = Wait(store, store.Initialize()); CHECK(r.ok);
            r = Wait(store, store.SubmitLegacy({{L"BeforeReset", {L"Role"}}})); CHECK(r.ok);
            const auto revision = r.snapshot->revision;
            r = Wait(store, store.ExecuteIdentity({{"action", "cmd_identity_reset_local"}, {"revision", revision - 1}, {"confirmed", true}}));
            CHECK(!r.ok && r.snapshot->FindName(L"BeforeReset"));
            r = Wait(store, store.ExecuteIdentity({{"action", "cmd_identity_reset_local"}, {"revision", revision}}));
            CHECK(!r.ok && r.snapshot->FindName(L"BeforeReset"));
            r = Wait(store, store.ExecuteIdentity({{"action", "cmd_identity_reset_local"}, {"revision", revision}, {"confirmed", true}}));
            CHECK(r.ok && r.snapshot->entities.empty() && r.snapshot->identifiers.empty());
            CHECK(r.snapshot->revision == revision + 1);
            CHECK(r.snapshot->identityMetadata.contains("localResetBackup"));
            const fs::path resetBackup = FromUtf8(r.snapshot->identityMetadata["localResetBackup"].get<std::string>());
            CHECK(fs::exists(resetBackup / "player_library.db"));
            std::ifstream legacyInput(reset.legacyPath, std::ios::binary);
            const std::string legacyText((std::istreambuf_iterator<char>(legacyInput)), {});
            CHECK(ParseLegacy(legacyText).empty());
            Stop(store);
            PlayerLibraryDatabase reopened(reset); CHECK(reopened.Initialize()->entities.empty());
            Options recovery{resetBackup / "player_library.db", resetBackup / "unused.ini", resetBackup / "unused.json", false};
            PlayerLibraryDatabase recovered(recovery); CHECK(recovered.Initialize()->FindName(L"BeforeReset"));
        }
        {
            Options stress{dir / "stress.db", dir / "stress.ini", dir / "stress.json", false};
            PlayerLibraryDatabase db(stress); db.Initialize(); LegacyLibrary library;
            for (int i = 0; i < 12200; ++i) library[L"Player" + std::to_wstring(i)] = {L"Unique" + std::to_wstring(i), L"Shared" + std::to_wstring(i % 1000)};
            auto started = std::chrono::steady_clock::now(); auto s = db.ReplaceLegacy(library);
            CHECK(s->entities.size() == 12200); CHECK(s->identifiers.size() == 13200);
            auto built = std::chrono::steady_clock::now();
            for (int i = 0; i < 100000; ++i) CHECK(s->Lookup(L"Unique500").candidates.size() == 1);
            auto finished = std::chrono::steady_clock::now();
            std::cout << "100x names: build=" << std::chrono::duration_cast<std::chrono::milliseconds>(built - started).count()
                << "ms, 100000 indexed lookups=" << std::chrono::duration_cast<std::chrono::milliseconds>(finished - built).count() << "ms\n";
        }
        std::cout << "player library core tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
