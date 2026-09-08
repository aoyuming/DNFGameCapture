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
#define CHECK(x) do { if (!(x)) throw std::runtime_error("Check failed: " #x); } while (false)
void Write(const fs::path& path, const std::string& text) { std::ofstream out(path, std::ios::binary); out << text; CHECK(out.good()); }
std::string Read(const fs::path& path) { std::ifstream in(path, std::ios::binary); CHECK(in.good()); return {std::istreambuf_iterator<char>(in), {}}; }
struct Connection {
    sqlite3* db = nullptr;
    explicit Connection(const fs::path& path) { CHECK(sqlite3_open(ToUtf8(path.wstring()).c_str(), &db) == SQLITE_OK); }
    ~Connection() { sqlite3_close(db); }
    void Exec(const char* sql) { CHECK(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK); }
    std::string Scalar(const char* sql) {
        sqlite3_stmt* raw = nullptr; CHECK(sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) == SQLITE_OK);
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> q(raw, sqlite3_finalize);
        CHECK(sqlite3_step(q.get()) == SQLITE_ROW);
        const auto* text = sqlite3_column_text(q.get(), 0); return text ? reinterpret_cast<const char*>(text) : "";
    }
};
Result Wait(PlayerLibraryStore& store, RequestId request) {
    for (int i = 0; i < 15000; ++i) {
        for (auto& result : store.Drain()) if (result.requestId == request) return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Store timed out");
}
void Stop(PlayerLibraryStore& store) {
    store.Shutdown();
    for (int i = 0; i < 1500 && !store.IsStopped(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(store.IsStopped());
}
std::vector<fs::path> Backups(const fs::path& dir, const std::string& ending) {
    std::vector<fs::path> result;
    for (const auto& file : fs::recursive_directory_iterator(dir)) {
        const auto name = file.path().filename().string();
        if (name.size() >= ending.size() && name.substr(name.size() - ending.size()) == ending) result.push_back(file.path());
    }
    return result;
}
template<class F> void Reject(F fn) { bool failed = false; try { fn(); } catch (const std::exception&) { failed = true; } CHECK(failed); }
int wmain(int argc, wchar_t** argv) {
    try {
        CHECK(argc == 2); const fs::path root = argv[1]; fs::create_directories(root);
        auto options = [&](const char* label) {
            auto dir = root / label; fs::create_directories(dir);
            Options o{dir / "production" / "player_library.db", dir / "alias_db.ini", dir / "groups.json", true};
            o.priorDatabasePath = dir / "prior.db"; return o;
        };
        const auto o = options("active-wal");
        const auto inactive = o.databasePath.parent_path().parent_path() / "inactive.db";
        Write(inactive, "not the selected database");
        Write(o.legacyPath, "OnlyInOldIni=(KeepInBackup)\n");
        Write(o.identityGroupsPath, "{old sidecar must survive in backup}");
        const auto ini = Read(o.legacyPath), sidecar = Read(o.identityGroupsPath);
        Options sourceOptions{o.priorDatabasePath, o.priorDatabasePath.parent_path() / "unused.ini", o.priorDatabasePath.parent_path() / "unused.json", false};
        PlayerLibraryDatabase source(sourceOptions); source.Initialize();
        auto saved = source.ImportV2({{"importSource", "https://test.example/library"}, {"revision", 17}, {"entities", json::array({
            {{"entityId", "cloudA"}, {"names", {"A", "AliasA"}}, {"gameIds", {"GameA", "Shared"}}},
            {{"entityId", "cloudB"}, {"names", {"B"}}, {"gameIds", {"GameB", "Shared"}}},
            {{"names", {"NameOnly"}}, {"gameIds", json::array()}}
        })}});
        saved = source.ExecuteIdentity({{"action", "merge"}, {"revision", saved->revision}, {"names", {"A", "B"}}});
        Connection live(o.priorDatabasePath);
        live.Exec("PRAGMA wal_autocheckpoint=0; INSERT INTO library_meta VALUES('custom_provenance','preserve-me');");
        saved = source.LoadSnapshot();
        CHECK(fs::file_size(fs::path(o.priorDatabasePath.wstring() + L"-wal")) > 0);
        const auto sourceBytes = Read(o.priorDatabasePath), walBytes = Read(fs::path(o.priorDatabasePath.wstring() + L"-wal"));
        SnapshotPtr copied;
        {
            PlayerLibraryStore target(o); auto result = Wait(target, target.Initialize()); CHECK(result.ok);
            copied = result.snapshot;
            CHECK(copied->v2Entities == saved->v2Entities && copied->identityMetadata == saved->identityMetadata);
            CHECK(copied->revision == saved->revision && copied->legacy == saved->legacy);
            CHECK(copied->FindName(L"NameOnly") && copied->legacy.at(L"NameOnly").empty());
            Stop(target);
        }
        CHECK(Read(o.priorDatabasePath) == sourceBytes && Read(fs::path(o.priorDatabasePath.wstring() + L"-wal")) == walBytes);
        CHECK(Read(inactive) == "not the selected database" && Read(o.identityGroupsPath) == sidecar);
        const auto backups = Backups(o.databasePath.parent_path(), ".prior.bak"); CHECK(backups.size() == 1);
        const auto backupBytes = Read(backups.front());
        { Connection copy(o.databasePath), backup(backups.front());
            CHECK(copy.Scalar("SELECT value FROM library_meta WHERE key='custom_provenance'") == "preserve-me");
            CHECK(copy.Scalar("SELECT value FROM library_meta WHERE key='remote_revisions'") == live.Scalar("SELECT value FROM library_meta WHERE key='remote_revisions'"));
            CHECK(backup.Scalar("SELECT value FROM library_meta WHERE key='identity_metadata'") == live.Scalar("SELECT value FROM library_meta WHERE key='identity_metadata'"));
        }
        bool iniBackup = false, metadataBackup = false;
        for (const auto& path : Backups(o.legacyPath.parent_path(), ".migrated.bak")) {
            iniBackup = iniBackup || Read(path) == ini; metadataBackup = metadataBackup || Read(path) == sidecar;
        }
        CHECK(iniBackup && metadataBackup);
        {
            PlayerLibraryDatabase target(o); auto s = target.Initialize(); CHECK(s->revision == copied->revision);
            const auto group = s->identityState["groups"][0]["groupId"];
            s = target.ExecuteIdentity({{"action", "unmerge"}, {"revision", s->revision}, {"groupId", group}, {"splitAll", true}});
            CHECK(s->FindName(L"A")->entityId == s->FindName(L"AliasA")->entityId);
            CHECK(s->FindName(L"A")->cloudId == "cloudA" && s->FindName(L"B")->cloudId == "cloudB");
            CHECK(s->legacy.at(L"A") == std::vector<std::wstring>({L"GameA", L"Shared"}));
            s = target.ReplaceLegacy({}); CHECK(s->entities.empty());
        }
        {
            // Once installed, even an inaccessible/corrupt selected source cannot resurrect deletions.
            auto restart = o; restart.priorDatabasePath = inactive;
            PlayerLibraryDatabase target(restart); CHECK(target.Initialize()->entities.empty());
            CHECK(Backups(o.databasePath.parent_path(), ".prior.bak").size() == 1);
        }
        CHECK(Read(backups.front()) == backupBytes);
        std::cout << "WAL backup, manual grouping/splits, names, cloud provenance, source preservation and restart guard passed.\n";

        const auto legacy = options("missing-prior");
        Write(legacy.legacyPath, "A=(X)\nB=(Y)\nNameOnly=\nA=(Z)\n");
        const json groups = {{"groups", json::array({{{"groupId", "manual-old"}, {"source", "manual"}, {"names", {"A", "B"}},
            {"beforeMerge", {{"A", {"X", "Z"}}, {"B", {"Y"}}}}}})}, {"autoSplitFingerprints", {"keep-exception"}}};
        Write(legacy.identityGroupsPath, groups.dump());
        const auto originalIni = Read(legacy.legacyPath), originalMetadata = Read(legacy.identityGroupsPath);
        std::uint64_t legacyRevision = 0;
        {
            PlayerLibraryDatabase target(legacy); const auto s = target.Initialize(); legacyRevision = s->revision;
            CHECK(s->legacy.size() == 3 && s->legacy.at(L"A").size() == 3);
            CHECK(s->FindName(L"A")->entityId == s->FindName(L"B")->entityId && s->legacy.at(L"NameOnly").empty());
            CHECK(s->identityMetadata["groups"][0]["beforeMerge"] == groups["groups"][0]["beforeMerge"]);
            CHECK(Read(legacy.legacyPath) == originalIni && Read(legacy.identityGroupsPath) == originalMetadata);
        }
        Write(legacy.legacyPath, "WeakerIni=(IgnoreMe)\n");
        { PlayerLibraryDatabase target(legacy); auto s = target.Initialize(); CHECK(s->revision == legacyRevision && !s->FindName(L"WeakerIni")); }
        CHECK(Backups(legacy.legacyPath.parent_path(), ".migrated.bak").size() == 2);

        for (bool migrationFailure : {false, true}) {
            const auto old = options(migrationFailure ? "old-sqlite-rollback" : "old-sqlite");
            auto input = old; input.databasePath = old.priorDatabasePath; input.priorDatabasePath.clear(); input.legacyExportEnabled = false;
            SnapshotPtr original;
            {
                PlayerLibraryDatabase db(input); db.Initialize();
                auto s = db.ReplaceLegacy({{L"A", {L"OldGame"}}, {L"B", {L"OtherGame"}}, {L"Empty", {}}});
                original = db.ExecuteIdentity({{"action", "merge"}, {"revision", s->revision}, {"names", {"A", "B"}}});
            }
            {
                Connection c(old.priorDatabasePath);
                c.Exec("PRAGMA journal_mode=WAL; UPDATE library_meta SET value='2' WHERE key='storage_version'; "
                    "UPDATE library_meta SET value=json_set(value,'$.autoGroupPolicyVersion',4) WHERE key='identity_metadata';");
                if (migrationFailure) c.Exec("CREATE TRIGGER fail_upgrade BEFORE UPDATE ON library_meta WHEN NEW.key='storage_version' BEGIN SELECT RAISE(ABORT,'injected upgrade failure'); END;");
                // Leave a valid committed WAL without a live writer, as after an old app exits without checkpointing.
                CHECK(sqlite3_db_config(c.db, SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE, 1, nullptr) == SQLITE_OK);
            }
            const auto wal = fs::path(old.priorDatabasePath.wstring() + L"-wal");
            CHECK(fs::exists(wal));
            const auto mainBytes = Read(old.priorDatabasePath), originalWal = Read(wal);
            const auto oldRevision = original->revision;
            {
                PlayerLibraryDatabase db(old);
                if (migrationFailure) {
                    Reject([&] { db.Initialize(); }); CHECK(!fs::exists(old.databasePath));
                    CHECK(Backups(old.databasePath.parent_path(), ".prior.bak").size() == 1);
                    CHECK(Backups(old.databasePath.parent_path(), ".migrating").empty());
                } else {
                    const auto s = db.Initialize();
                    CHECK(s->revision == oldRevision + 1 && s->identityMetadata["autoGroupPolicyVersion"] == 5);
                    CHECK(s->legacy == original->legacy);
                    CHECK(s->identityMetadata["groups"] == original->identityMetadata["groups"]);
                    CHECK(s->FindName(L"A")->entityId == s->FindName(L"B")->entityId);
                    CHECK(s->FindName(L"Empty") && s->legacy.at(L"Empty").empty());
                    CHECK(db.Initialize()->revision == s->revision);
                }
            }
            CHECK(Read(old.priorDatabasePath) == mainBytes && Read(wal) == originalWal);
            {
                Connection c(old.priorDatabasePath);
                CHECK(c.Scalar("SELECT value FROM library_meta WHERE key='storage_version'") == "2");
                CHECK(c.Scalar("SELECT value FROM library_meta WHERE key='revision'") == std::to_string(oldRevision));
                if (migrationFailure) c.Exec("DROP TRIGGER fail_upgrade");
            }
            PlayerLibraryDatabase retry(old); CHECK(retry.Initialize()->revision == oldRevision + 1);
            Connection installed(old.databasePath); CHECK(installed.Scalar("SELECT value FROM library_meta WHERE key='storage_version'") == "3");
        }
        std::cout << "Old SQLite storage 2 / policy 4 staging upgrade, closed WAL preservation, rollback and retry passed.\n";

        for (const auto* label : {"corrupt-source", "unsupported-source", "corrupt-identity", "inconsistent-identity", "orphan-wal"}) {
            const auto bad = options(label); Write(bad.legacyPath, "DoNotOverwrite=(Safe)\n");
            if (std::string(label) == "corrupt-source") Write(bad.priorDatabasePath, "broken database");
            if (std::string(label) == "unsupported-source") { Connection c(bad.priorDatabasePath); c.Exec("CREATE TABLE unrelated(value TEXT); INSERT INTO unrelated VALUES('keep');"); }
            if (std::string(label) == "corrupt-identity") Write(bad.identityGroupsPath, "{broken");
            if (std::string(label) == "inconsistent-identity") Write(bad.identityGroupsPath, groups.dump());
            if (std::string(label) == "orphan-wal") Write(fs::path(bad.priorDatabasePath.wstring() + L"-wal"), "needs recovery");
            const auto before = Read(bad.legacyPath);
            const auto beforeSource = fs::exists(bad.priorDatabasePath) ? Read(bad.priorDatabasePath) : "";
            PlayerLibraryStore target(bad); auto r = Wait(target, target.Initialize());
            CHECK(!r.ok && !r.error.empty() && !r.snapshot->persisted && !fs::exists(bad.databasePath));
            CHECK(Read(bad.legacyPath) == before);
            if (!beforeSource.empty()) { CHECK(Read(bad.priorDatabasePath) == beforeSource); CHECK(r.snapshot->entities.empty()); }
            auto write = Wait(target, target.SubmitLegacy({})); CHECK(!write.ok && Read(bad.legacyPath) == before);
            Stop(target);
        }
        std::cout << "Missing-source INI migration, name-only entries, conservative failures and no export after failure passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
