#include "PlayerLibraryDatabase.h"
#include "PlayerIdentityGroupService.h"
#include "third_party/sqlite/sqlite3.h"
#define NOMINMAX
#include <Windows.h>
#include <icu.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>

namespace dnf::player_library {
using json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
constexpr std::size_t MaxNames = 100000, MaxIds = 1000000, MaxBytes = 64 * 1024 * 1024;
void Require(bool condition, const char* message) { if (!condition) throw std::invalid_argument(message); }
void Sql(sqlite3* db, const char* sql) {
    char* message = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK) { std::string error = message ? message : "SQLite error"; sqlite3_free(message); throw std::runtime_error(error); }
}
class Statement {
    sqlite3_stmt* stmt_ = nullptr;
public:
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    void Bind(int index, std::int64_t value) { if (sqlite3_bind_int64(stmt_, index, value) != SQLITE_OK) throw std::runtime_error("SQLite integer bind failed"); }
    void Bind(int index, const std::string& value) { if (sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) throw std::runtime_error("SQLite text bind failed"); }
    bool Step() {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) return true;
        if (rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(stmt_)));
        return false;
    }
    void Run() { Step(); sqlite3_reset(stmt_); sqlite3_clear_bindings(stmt_); }
    std::int64_t Int(int col) const { return sqlite3_column_int64(stmt_, col); }
    std::string Text(int col) const { auto p = sqlite3_column_text(stmt_, col); return p ? std::string(reinterpret_cast<const char*>(p), sqlite3_column_bytes(stmt_, col)) : ""; }
};
struct Transaction {
    sqlite3* db;
    bool committed = false;
    explicit Transaction(sqlite3* value, bool write = true) : db(value) { Sql(db, write ? "BEGIN IMMEDIATE" : "BEGIN"); }
    ~Transaction() { if (!committed) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); }
    void Commit() { Sql(db, "COMMIT"); committed = true; }
};
std::string ReadFile(const fs::path& path) {
    if (!fs::exists(path)) return {};
    Require(fs::is_regular_file(path), "Expected regular input file");
    const auto size = fs::file_size(path); Require(size <= MaxBytes, "Input file exceeds 64 MiB");
    std::ifstream file(path, std::ios::binary); if (!file) throw std::runtime_error("Cannot open library input");
    std::string result(static_cast<std::size_t>(size), '\0');
    if (size && !file.read(result.data(), static_cast<std::streamsize>(size))) throw std::runtime_error("Cannot read library input");
    return result;
}
std::wstring Suffix() { return L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(std::chrono::system_clock::now().time_since_epoch().count()); }
void Backup(const fs::path& path, const std::wstring& suffix) {
    if (fs::exists(path)) fs::copy_file(path, fs::path(path.wstring() + suffix + L".migrated.bak"));
}
void CheckDatabase(sqlite3* db) {
    Statement check(db, "PRAGMA integrity_check");
    Require(check.Step() && check.Text(0) == "ok", "Player library integrity check failed");
    Statement references(db, "PRAGMA foreign_key_check");
    Require(!references.Step(), "Player library contains broken references");
}
void BackupPriorDatabase(const fs::path& source, const fs::path& destination) {
    Require(fs::is_regular_file(source), "Prior player library is not a regular file");
    Require(!fs::exists(destination), "Prior player library backup already exists");
    auto open = [](const fs::path& path, int flags) {
        sqlite3* raw = nullptr;
        const int rc = sqlite3_open_v2(ToUtf8(path.wstring()).c_str(), &raw, flags | SQLITE_OPEN_NOMUTEX, nullptr);
        std::unique_ptr<sqlite3, decltype(&sqlite3_close_v2)> handle(raw, sqlite3_close_v2);
        if (rc != SQLITE_OK) throw std::runtime_error(raw ? sqlite3_errmsg(raw) : "Cannot open prior library backup");
        sqlite3_busy_timeout(raw, 1000);
        return handle;
    };
    // SQLite's backup API includes committed WAL data. Never copy or remove the
    // source's main/WAL files, and never run schema migrations on that connection.
    auto input = open(source, SQLITE_OPEN_READONLY);
    try {
        auto output = open(destination, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
        Sql(output.get(), "PRAGMA synchronous=FULL;");
        auto* backup = sqlite3_backup_init(output.get(), "main", input.get(), "main");
        const int copied = backup ? sqlite3_backup_step(backup, -1) : SQLITE_ERROR;
        const int finished = backup ? sqlite3_backup_finish(backup) : SQLITE_ERROR;
        Require(copied == SQLITE_DONE && finished == SQLITE_OK, "Cannot back up prior player library; source may be busy or damaged");
        Sql(output.get(), "PRAGMA journal_mode=DELETE;");
        CheckDatabase(output.get());
    } catch (...) {
        std::error_code ignored; fs::remove(destination, ignored);
        fs::remove(fs::path(destination.wstring() + L"-journal"), ignored);
        throw;
    }
}
void AtomicWrite(const fs::path& path, const std::string& value,
    const char* failureMessage = "Legacy compatibility export failed; database is committed") {
    const fs::path temporary(path.wstring() + Suffix() + L".tmp");
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create legacy export temporary file");
    DWORD written = 0;
    const bool ok = WriteFile(file, value.data(), static_cast<DWORD>(value.size()), &written, nullptr) && written == value.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); throw std::runtime_error(failureMessage);
    }
}
void ValidateText(const std::wstring& value, bool name) {
    Require(!Trim(value).empty(), name ? "Empty player name" : "Empty identifier");
    Require(value.size() <= (name ? 256u : 1024u), "Text exceeds length limit");
    for (auto c : value) Require(c >= 0x20 && c != 0x7f, "Control character is not allowed");
    if (name) Require(value.find(L'=') == std::wstring::npos, "Name cannot contain '='");
    ToUtf8(value);
}
class SystemIcu {
    HMODULE module_ = nullptr;
    template<class Function> Function Resolve(const char* name) const noexcept {
        const auto address = GetProcAddress(module_, name);
        Function result = nullptr;
        static_assert(sizeof(result) == sizeof(address));
        std::memcpy(&result, &address, sizeof(result));
        return result;
    }
public:
    decltype(&ubrk_open) open = nullptr;
    decltype(&ubrk_first) first = nullptr;
    decltype(&ubrk_next) next = nullptr;
    decltype(&ubrk_close) close = nullptr;
    decltype(&u_charType) charType = nullptr;
    SystemIcu() noexcept {
        // Never search the executable directory or PATH for this optional dependency.
        module_ = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module_) return;
        open = Resolve<decltype(open)>("ubrk_open");
        first = Resolve<decltype(first)>("ubrk_first");
        next = Resolve<decltype(next)>("ubrk_next");
        close = Resolve<decltype(close)>("ubrk_close");
        charType = Resolve<decltype(charType)>("u_charType");
    }
    ~SystemIcu() { if (module_) FreeLibrary(module_); }
    SystemIcu(const SystemIcu&) = delete;
    SystemIcu& operator=(const SystemIcu&) = delete;
    bool Available() const noexcept { return open && first && next && close && charType; }
};
const SystemIcu& CachedSystemIcu() { static const SystemIcu api; return api; }

void ValidateCloudNameFallback(const std::wstring& text) {
    std::size_t count = 0;
    bool visible = false;
    for (int32_t pos = 0; pos < static_cast<int32_t>(text.size());) {
        UChar32 cp; U16_NEXT(reinterpret_cast<const UChar*>(text.data()), pos, static_cast<int32_t>(text.size()), cp);
        Require(cp >= 0 && !(cp >= 0xd800 && cp <= 0xdfff), "Invalid UTF-16 cloud name");
        Require(++count <= 64, "Without system ICU, cloud names are limited to 64 Unicode scalar values");
        // NLS character categories are reliable for BMP text. Do not guess newer
        // supplementary-plane categories on systems without a grapheme engine.
        Require(cp <= 0xffff, "System ICU is unavailable; supplementary Unicode cloud names require a newer Windows version");
        const bool forbidden = cp < 0x20 || (cp >= 0x7f && cp <= 0x9f) || cp == 0xad ||
            (cp >= 0x600 && cp <= 0x605) || cp == 0x61c || cp == 0x6dd || cp == 0x70f ||
            (cp >= 0x890 && cp <= 0x891) || cp == 0x8e2 || cp == 0x180e ||
            (cp >= 0x200b && cp <= 0x200f) || (cp >= 0x2028 && cp <= 0x202e) ||
            (cp >= 0x2060 && cp <= 0x206f) || cp == 0xfeff || (cp >= 0xfff9 && cp <= 0xfffb);
        Require(!forbidden, "Cloud name contains forbidden Unicode characters");
        const wchar_t ch = static_cast<wchar_t>(cp); WORD type1 = 0, type3 = 0;
        Require(GetStringTypeW(CT_CTYPE1, &ch, 1, &type1) && GetStringTypeW(CT_CTYPE3, &ch, 1, &type3), "Cannot classify cloud name on this Windows version");
        Require(!(type1 & C1_CNTRL), "Cloud name contains forbidden Unicode characters");
        const bool mark = (type3 & (C3_NONSPACING | C3_VOWELMARK)) != 0;
        const bool base = !mark && ((type1 & (C1_ALPHA | C1_DIGIT | C1_PUNCT)) || (type3 & C3_SYMBOL));
        Require(base || mark || (type1 & (C1_SPACE | C1_BLANK)), "System ICU is unavailable; this complex Unicode cloud name requires a newer Windows version");
        visible = visible || base;
    }
    Require(count > 0 && visible, "Cloud name has no visible base character");
}
void ValidateCloudName(const std::wstring& text, bool forceFallback = false) {
    if (forceFallback) { ValidateCloudNameFallback(text); return; }
    const auto& api = CachedSystemIcu();
    if (!api.Available()) { ValidateCloudNameFallback(text); return; }
    UErrorCode status = U_ZERO_ERROR;
    auto release = [&](UBreakIterator* value) { api.close(value); };
    std::unique_ptr<UBreakIterator, decltype(release)> breaks(api.open(UBRK_CHARACTER, "zh_CN", reinterpret_cast<const UChar*>(text.data()), static_cast<int32_t>(text.size()), &status), release);
    if (U_FAILURE(status) || !breaks) throw std::runtime_error("Cannot validate cloud name graphemes");
    std::size_t count = 0; api.first(breaks.get());
    while (api.next(breaks.get()) != UBRK_DONE) ++count;
    Require(count >= 1 && count <= 64, "Cloud name must contain 1 to 64 graphemes");
    bool visible = false;
    for (int32_t pos = 0; pos < static_cast<int32_t>(text.size());) {
        UChar32 cp; U16_NEXT(reinterpret_cast<const UChar*>(text.data()), pos, static_cast<int32_t>(text.size()), cp);
        const auto category = api.charType(cp);
        Require(category != U_CONTROL_CHAR && category != U_FORMAT_CHAR && category != U_LINE_SEPARATOR && category != U_PARAGRAPH_SEPARATOR, "Cloud name contains forbidden Unicode characters");
        const auto mask = U_MASK(category);
        if (mask & (U_GC_L_MASK | U_GC_N_MASK | U_GC_P_MASK | U_GC_S_MASK)) visible = true;
    }
    Require(visible, "Cloud name has no visible base character");
}
std::vector<std::wstring> ReadStrings(const json& value, bool names = false) {
    if (value.is_string() && !names) return ParseIds(FromUtf8(value.get<std::string>()));
    Require(value.is_array(), "Expected string array");
    Require(value.size() <= MaxNames, "Array exceeds item limit");
    std::vector<std::wstring> result;
    std::set<std::wstring> seen;
    for (const auto& item : value) {
        Require(item.is_string(), "Array item must be a string");
        auto text = Trim(FromUtf8(item.get<std::string>())); ValidateText(text, names);
        if (seen.insert(text).second) result.push_back(std::move(text));
    }
    return result;
}
json Strings(const std::vector<std::wstring>& values) { auto result = json::array(); for (const auto& v : values) result.push_back(ToUtf8(v)); return result; }
template<class T> void AddUnique(std::vector<T>& values, const T& value) { if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value); }
std::set<std::wstring> Keys(const std::vector<std::wstring>& ids) { std::set<std::wstring> keys; for (const auto& id : ids) keys.insert(CanonicalKey(id, IdentifierKind::Game)); return keys; }
bool ValidCloudId(const std::string& id) {
    return !id.empty() && id.size() <= 128 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
void ValidateMetadata(const json& value) {
    Require(value.is_object(), "Identity metadata must be an object");
    Require(value.dump().size() <= MaxBytes, "Identity metadata exceeds size limit");
    if (value.contains("groups")) {
        Require(value["groups"].is_array(), "Identity groups must be an array");
        std::set<std::string> groupIds; std::set<std::wstring> claimed;
        for (const auto& group : value["groups"]) {
            Require(group.is_object() && group.contains("groupId") && group["groupId"].is_string(), "Invalid identity group");
            const auto id = group["groupId"].get<std::string>(); Require(!id.empty() && groupIds.insert(id).second, "Duplicate or empty group ID");
            if (group.contains("source")) Require(group["source"].is_string(), "Identity group source must be a string");
            Require(group.contains("names"), "Identity group is missing names");
            for (const auto& name : ReadStrings(group["names"], true)) Require(claimed.insert(name).second, "Overlapping explicit identity groups");
            for (const auto* field : {"beforeMerge"}) if (group.contains(field)) {
                Require(group[field].is_object(), "Invalid split snapshot");
                for (const auto& [name, ids] : group[field].items()) { ValidateText(FromUtf8(name), true); ReadStrings(ids); }
            }
            if (group.contains("beforeEntities")) {
                Require(group["beforeEntities"].is_object(), "Invalid original entity snapshot");
                for (const auto& [name, original] : group["beforeEntities"].items()) {
                    ValidateText(FromUtf8(name), true); Require(original.is_object(), "Original entity must be an object");
                    if (original.contains("entityId")) Require(original["entityId"].is_number_integer() && original["entityId"].get<EntityId>() > 0, "Original entityId must be a positive integer");
                    if (original.contains("cloudId")) Require(original["cloudId"].is_string() && ValidCloudId(original["cloudId"].get<std::string>()), "Invalid original cloudId");
                }
            }
        }
    }
    if (value.contains("autoSplitFingerprints")) {
        Require(value["autoSplitFingerprints"].is_array(), "Invalid ignored suggestions");
        for (const auto& x : value["autoSplitFingerprints"]) Require(x.is_string(), "Invalid ignored fingerprint");
    }
    if (value.contains("autoSplitNameSets")) {
        Require(value["autoSplitNameSets"].is_array(), "Invalid split exception sets");
        for (const auto& names : value["autoSplitNameSets"]) Require(ReadStrings(names, true).size() >= 2, "Split exception requires two names");
    }
}
void SynchronizeManagedMembers(Snapshot& s) {
    if (!s.identityMetadata.contains("groups")) return;
    std::unordered_map<std::wstring, const PlayerEntity*> owners;
    for (const auto& entity : s.entities) for (const auto& name : entity.names) owners.emplace(name, &entity);
    for (auto& group : s.identityMetadata["groups"]) {
        if (group.value("source", "manual") == "automatic" && !group.contains("beforeEntities")) continue;
        auto names = ReadStrings(group["names"], true);
        std::set<std::wstring> seen(names.begin(), names.end());
        std::set<EntityId> visited;
        for (std::size_t i = 0; i < names.size(); ++i) {
            const auto owner = owners.find(names[i]);
            if (owner != owners.end() && visited.insert(owner->second->entityId).second)
                for (const auto& actual : owner->second->names) if (seen.insert(actual).second) names.push_back(actual);
        }
        group["names"] = Strings(names);
    }
}
template<class T> bool SameSet(std::vector<T> left, std::vector<T> right) {
    std::sort(left.begin(), left.end()); std::sort(right.begin(), right.end()); return left == right;
}
bool SameNormalizedState(const Snapshot& left, const Snapshot& right) {
    if (left.entities.size() != right.entities.size() || left.identityMetadata != right.identityMetadata) return false;
    for (const auto& entity : left.entities) {
        const auto found = right.entityIndex.find(entity.entityId); if (found == right.entityIndex.end()) return false;
        const auto& old = right.entities[found->second];
        if (entity.cloudId != old.cloudId || !SameSet(entity.names, old.names) || !SameSet(entity.gameIds, old.gameIds)) return false;
    }
    return true;
}
bool SameIdentifierStorage(const Snapshot& left, const Snapshot& right) {
    if (left.identifiers.size() != right.identifiers.size()) return false;
    for (const auto& id : left.identifiers) {
        const auto* old = right.FindIdentifier(id.identifierId);
        if (!old || id.kind != old->kind || id.canonicalKey != old->canonicalKey || id.displayText != old->displayText || !SameSet(id.spellings, old->spellings)) return false;
    }
    return true;
}
}

#ifdef DNF_PLAYER_LIBRARY_TESTING
void ValidateCloudNameForTesting(const std::wstring& text, bool forceFallback) {
    ValidateText(text, true); ValidateCloudName(text, forceFallback);
}
bool HasSystemIcuForTesting() { return CachedSystemIcu().Available(); }
#endif

Options ResolveOptions(Options o) {
    std::wstring exe(32768, L'\0'); DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    Require(length > 0 && length < exe.size(), "Cannot resolve executable path"); exe.resize(length);
    if (o.legacyPath.empty()) o.legacyPath = fs::path(exe).parent_path() / L"alias_db.ini";
    if (o.identityGroupsPath.empty()) o.identityGroupsPath = o.legacyPath.parent_path() / L"player_identity_groups.json";
    if (o.databasePath.empty()) {
        DWORD size = GetEnvironmentVariableW(L"APPDATA", nullptr, 0); Require(size > 1, "APPDATA is not set");
        std::wstring appdata(size, L'\0'); GetEnvironmentVariableW(L"APPDATA", appdata.data(), size); appdata.resize(size - 1);
        o.databasePath = fs::path(appdata) / L"DNFGameCapture" / L"player_library.db";
    }
    o.databasePath = fs::absolute(o.databasePath); o.legacyPath = fs::absolute(o.legacyPath); o.identityGroupsPath = fs::absolute(o.identityGroupsPath);
    if (!o.priorDatabasePath.empty()) o.priorDatabasePath = fs::absolute(o.priorDatabasePath);
    Require(o.databasePath != o.legacyPath && o.databasePath != o.identityGroupsPath && o.legacyPath != o.identityGroupsPath, "Library paths must differ");
    return o;
}

struct PlayerLibraryDatabase::Impl {
    Options options;
    std::string warning;
    sqlite3* db = nullptr;
    explicit Impl(Options o) : options(ResolveOptions(std::move(o))) {}
    ~Impl() { Close(); }
    void Close() { if (db) { sqlite3_close_v2(db); db = nullptr; } }
    void Open(const fs::path& path, bool create) {
        const auto utf8 = ToUtf8(path.wstring());
        if (sqlite3_open_v2(utf8.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX | (create ? SQLITE_OPEN_CREATE : 0), nullptr) != SQLITE_OK) {
            std::string error = db ? sqlite3_errmsg(db) : "Cannot open SQLite"; Close(); throw std::runtime_error(error);
        }
        sqlite3_busy_timeout(db, 1000); Sql(db, "PRAGMA foreign_keys=ON; PRAGMA synchronous=FULL;");
    }
    std::string Meta(const char* key) {
        Statement q(db, "SELECT value FROM library_meta WHERE key=?"); q.Bind(1, std::string(key)); return q.Step() ? q.Text(0) : "";
    }
    void SetMeta(const char* key, const std::string& value) {
        Statement q(db, "INSERT INTO library_meta(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"); q.Bind(1, std::string(key)); q.Bind(2, value); q.Run();
    }
    EntityId NextEntity() { Statement q(db, "SELECT COALESCE(MAX(entity_id),0)+1 FROM player_entities"); q.Step(); return q.Int(0); }
    void Schema() {
        Sql(db, R"sql(
CREATE TABLE library_meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);
CREATE TABLE player_entities(entity_id INTEGER PRIMARY KEY,cloud_id TEXT,deleted INTEGER NOT NULL DEFAULT 0);
CREATE UNIQUE INDEX active_cloud_id ON player_entities(cloud_id) WHERE deleted=0 AND cloud_id IS NOT NULL;
CREATE TABLE player_names(name TEXT PRIMARY KEY COLLATE BINARY,entity_id INTEGER NOT NULL REFERENCES player_entities(entity_id));
CREATE INDEX names_entity ON player_names(entity_id);
CREATE TABLE identifiers(identifier_id INTEGER PRIMARY KEY,kind INTEGER NOT NULL CHECK(kind=0),canonical_key TEXT NOT NULL COLLATE BINARY,display_text TEXT NOT NULL,UNIQUE(kind,canonical_key));
CREATE TABLE identifier_spellings(identifier_id INTEGER NOT NULL REFERENCES identifiers(identifier_id),raw_text TEXT NOT NULL,PRIMARY KEY(identifier_id,raw_text));
CREATE TABLE entity_identifiers(entity_id INTEGER NOT NULL REFERENCES player_entities(entity_id),identifier_id INTEGER NOT NULL REFERENCES identifiers(identifier_id),position INTEGER NOT NULL,PRIMARY KEY(entity_id,identifier_id));
CREATE INDEX identifier_owners ON entity_identifiers(identifier_id);
)sql");
        SetMeta("storage_version", "3"); SetMeta("normalizer_version", "1"); SetMeta("revision", "0"); SetMeta("identity_metadata", "{}");
    }
    bool MigrateSchema() {
        const auto version = Meta("storage_version");
        Require((version == "2" || version == "3") && Meta("normalizer_version") == "1", "Unsupported library storage/normalizer version");
        if (version == "3") return false;
        auto metadata = json::parse(Meta("identity_metadata"));
        PurgeRetiredIdentityFields(metadata);
        std::set<IdentifierId> gameIds;
        {
            Statement ids(db, "SELECT identifier_id FROM identifiers WHERE kind=0");
            while (ids.Step()) gameIds.insert(ids.Int(0));
        }
        // Never decode a retired numeric reference through the game-ID namespace.
        if (metadata.contains("groups")) for (auto& group : metadata["groups"])
            if (group.contains("beforeMerge")) for (auto& [name, refs] : group["beforeMerge"].items()) {
                Require(refs.is_array(), "Corrupt split snapshot");
                refs.erase(std::remove_if(refs.begin(), refs.end(), [&](const auto& ref) {
                    return ref.is_number_integer() && !gameIds.count(ref.template get<IdentifierId>());
                }), refs.end());
            }
        // The caller owns one transaction covering schema, data, policy and revision.
        // Copy dependent tables first; no foreign-key disabling or live-file mutation.
        Sql(db, R"sql(
CREATE TABLE identifiers_v3(identifier_id INTEGER PRIMARY KEY,kind INTEGER NOT NULL CHECK(kind=0),canonical_key TEXT NOT NULL COLLATE BINARY,display_text TEXT NOT NULL,UNIQUE(kind,canonical_key));
CREATE TABLE identifier_spellings_v3(identifier_id INTEGER NOT NULL REFERENCES identifiers_v3(identifier_id),raw_text TEXT NOT NULL,PRIMARY KEY(identifier_id,raw_text));
CREATE TABLE entity_identifiers_v3(entity_id INTEGER NOT NULL REFERENCES player_entities(entity_id),identifier_id INTEGER NOT NULL REFERENCES identifiers_v3(identifier_id),position INTEGER NOT NULL,PRIMARY KEY(entity_id,identifier_id));
INSERT INTO identifiers_v3 SELECT * FROM identifiers WHERE kind=0;
INSERT INTO identifier_spellings_v3 SELECT s.* FROM identifier_spellings s JOIN identifiers_v3 i USING(identifier_id);
INSERT INTO entity_identifiers_v3 SELECT e.* FROM entity_identifiers e JOIN identifiers_v3 i USING(identifier_id);
DROP TABLE entity_identifiers;
DROP TABLE identifier_spellings;
DROP TABLE identifiers;
ALTER TABLE identifiers_v3 RENAME TO identifiers;
ALTER TABLE identifier_spellings_v3 RENAME TO identifier_spellings;
ALTER TABLE entity_identifiers_v3 RENAME TO entity_identifiers;
CREATE INDEX identifier_owners ON entity_identifiers(identifier_id);
DELETE FROM library_meta WHERE lower(key) LIKE '%adventure%';
)sql");
        SetMeta("identity_metadata", metadata.dump());
        SetMeta("storage_version", "3");
        return true;
    }
    IdentifierId Intern(Snapshot& s, const std::wstring& text, IdentifierKind kind) {
        ValidateText(text, false); const auto key = CanonicalKey(text, kind);
        // Pool maps are maintained during each command to avoid scanning the library per ID.
        auto found = internIndex.find({kind, key});
        if (found != internIndex.end()) {
            auto& value = s.identifiers[found->second];
            if (text != value.displayText) AddUnique(value.spellings, text);
            return value.identifierId;
        }
        Require(s.identifiers.size() < MaxIds, "Identifier pool exceeds limit");
        Identifier value; value.identifierId = ++nextIdentifier; value.kind = kind; value.canonicalKey = key; value.displayText = Trim(text);
        if (text != value.displayText) value.spellings.push_back(text);
        internIndex.emplace(std::make_pair(kind, key), s.identifiers.size()); s.identifiers.push_back(std::move(value));
        return nextIdentifier;
    }
    std::map<std::pair<IdentifierKind, std::wstring>, std::size_t> internIndex;
    IdentifierId nextIdentifier = 0;
    void PrepareIntern(const Snapshot& s) {
        internIndex.clear(); nextIdentifier = 0;
        for (std::size_t i = 0; i < s.identifiers.size(); ++i) {
            internIndex.emplace(std::make_pair(s.identifiers[i].kind, s.identifiers[i].canonicalKey), i);
            nextIdentifier = (std::max)(nextIdentifier, s.identifiers[i].identifierId);
        }
    }
    std::vector<IdentifierId> InternIds(Snapshot& s, const std::vector<std::wstring>& ids, IdentifierKind kind) {
        std::vector<IdentifierId> result; std::set<IdentifierId> seen;
        for (const auto& id : ids) { auto ref = Intern(s, id, kind); if (seen.insert(ref).second) result.push_back(ref); }
        return result;
    }
    json EncodeMetadata(Snapshot& s) {
        auto metadata = s.identityMetadata;
        if (metadata.contains("groups")) for (auto& group : metadata["groups"]) {
            for (const auto* field : {"beforeMerge"}) if (group.contains(field)) {
                for (auto& [name, values] : group[field].items()) values = InternIds(s, ReadStrings(values), IdentifierKind::Game);
            }
        }
        return metadata;
    }
    void DecodeMetadata(Snapshot& s) {
        std::unordered_map<IdentifierId, const Identifier*> ids;
        for (const auto& id : s.identifiers) ids.emplace(id.identifierId, &id);
        if (s.identityMetadata.contains("groups")) for (auto& group : s.identityMetadata["groups"]) {
            for (const auto* field : {"beforeMerge"}) if (group.contains(field)) {
                for (auto& [name, values] : group[field].items()) {
                    auto decoded = json::array(); Require(values.is_array(), "Corrupt split snapshot");
                    for (const auto& ref : values) { Require(ref.is_number_integer() && ids.count(ref.get<IdentifierId>()), "Corrupt split identifier reference"); decoded.push_back(ToUtf8(ids.at(ref.get<IdentifierId>())->displayText)); }
                    values = std::move(decoded);
                }
            }
        }
    }
    void WarnAutomaticReview(const Snapshot& s) {
        if (s.identityMetadata.contains("groups")) for (const auto& group : s.identityMetadata["groups"])
            if (group.value("autoGroupNeedsReview", false)) {
                warning = u8"旧版自动同人分组需要人工复核：原始依据不完整或已被编辑，现有分组和数据已保留，暂不参与自动匹配。";
                return;
            }
    }
    bool MigrateAutomaticPolicy(Snapshot& s) {
        const auto version = s.identityMetadata.value("autoGroupPolicyVersion", json());
        if (version.is_number_integer() && version.get<std::int64_t>() >= dnf::identity::AUTO_GROUP_POLICY_VERSION) return false;
        BuildSnapshotViews(s);
        auto groups = json::array();
        std::set<EntityId> removed, reserved;
        for (const auto& e : s.entities) reserved.insert(e.entityId);
        std::vector<PlayerEntity> restored;
        for (auto group : s.identityMetadata.value("groups", json::array())) {
            if (group.value("source", "manual") != "automatic") { groups.push_back(std::move(group)); continue; }
            const auto names = ReadStrings(group["names"], true);
            const std::set<std::wstring> nameSet(names.begin(), names.end());
            const auto before = group.value("beforeMerge", json::object());
            const auto originals = group.value("beforeEntities", json::object());
            bool safe = names.size() >= 2;
            std::set<EntityId> members;
            std::set<std::string> currentCloud, savedCloud;
            std::set<IdentifierId> currentGame, savedGame;
            std::map<EntityId, PlayerEntity> prior;
            for (const auto& name : names) {
                const auto* owner = s.FindName(name); const auto key = ToUtf8(name);
                if (!owner || !before.contains(key)) { safe = false; break; }
                if (members.insert(owner->entityId).second) {
                    if (!std::all_of(owner->names.begin(), owner->names.end(), [&](const auto& n) { return nameSet.count(n) != 0; })) { safe = false; break; }
                    currentGame.insert(owner->gameIds.begin(), owner->gameIds.end());
                    if (owner->cloudId) currentCloud.insert(*owner->cloudId);
                }
                EntityId id = owner->entityId;
                auto cloud = owner->cloudId;
                if (group.contains("beforeEntities")) {
                    if (!originals.contains(key) || !originals[key].contains("entityId")) { safe = false; break; }
                    id = originals[key]["entityId"].get<EntityId>(); cloud.reset();
                    if (originals[key].contains("cloudId")) cloud = originals[key]["cloudId"].get<std::string>();
                } else if (owner->names.size() > 1) { safe = false; break; }
                auto [it, inserted] = prior.emplace(id, PlayerEntity{});
                auto& e = it->second;
                if (inserted) { e.entityId = id; e.cloudId = cloud; }
                else if (e.cloudId != cloud) { safe = false; break; }
                e.names.push_back(name);
                for (auto ref : InternIds(s, ReadStrings(before[key]), IdentifierKind::Game)) {
                    savedGame.insert(ref); AddUnique(e.gameIds, ref);
                }
            }
            // Do not guess ownership of post-merge edits, new aliases, or missing
            // provenance. Original entity IDs keep manual/cloud alias sets intact.
            safe = safe && currentGame == savedGame;
            for (const auto& [id, e] : prior) {
                if (reserved.count(id) && !members.count(id)) safe = false;
                // A grouped first cloud pull historically tagged only its anchor.
                // Partial or shared cloud provenance cannot prove separate owners.
                if (e.cloudId) { if (!savedCloud.insert(*e.cloudId).second) safe = false; }
                else if (!currentCloud.empty()) safe = false;
            }
            for (const auto& cloud : currentCloud) if (!savedCloud.count(cloud)) safe = false;
            if (!safe) {
                group["autoGroupNeedsReview"] = true;
                groups.push_back(std::move(group));
                continue;
            }
            removed.insert(members.begin(), members.end());
            for (auto id : members) reserved.erase(id);
            for (auto& [id, e] : prior) { reserved.insert(id); restored.push_back(std::move(e)); }
        }
        if (s.identityMetadata.contains("groups")) s.identityMetadata["groups"] = std::move(groups);
        s.identityMetadata["autoGroupPolicyVersion"] = dnf::identity::AUTO_GROUP_POLICY_VERSION;
        if (!restored.empty()) {
            s.entities.erase(std::remove_if(s.entities.begin(), s.entities.end(), [&](const auto& e) { return removed.count(e.entityId) != 0; }), s.entities.end());
            for (auto& e : restored) s.entities.push_back(std::move(e));
            std::sort(s.entities.begin(), s.entities.end(), [](const auto& a, const auto& b) { return a.entityId < b.entityId; });
            CoalesceAutomatic(s);
        }
        return true;
    }
    void CoalesceAutomatic(Snapshot& s) {
        BuildSnapshotViews(s);
        const auto analysis = AnalyzeIdentityEvidence(s);
        const auto existingGroups = s.identityMetadata.value("groups", json::array());
        std::unordered_map<std::wstring, std::size_t> groupByName;
        for (std::size_t i = 0; i < existingGroups.size(); ++i)
            for (const auto& name : ReadStrings(existingGroups[i]["names"], true)) groupByName[name] = i;
        std::set<std::size_t> removedGroups;
        std::set<EntityId> removedEntities;
        std::vector<PlayerEntity> mergedEntities;
        auto newGroups = json::array();
        for (const auto& candidate : analysis.exactGroups) {
            std::set<EntityId> members;
            for (const auto& name : candidate.names) members.insert(s.FindName(name)->entityId);
            if (members.size() < 2) continue;
            const std::set<std::wstring> names(candidate.names.begin(), candidate.names.end());
            bool complete = true;
            for (auto id : members) {
                const auto& e = s.entities.at(s.entityIndex.at(id));
                if (removedEntities.count(id) || !std::all_of(e.names.begin(), e.names.end(), [&](const auto& name) { return names.count(name) != 0; })) complete = false;
            }
            // A persisted entity is indivisible here. Every saved constituent must
            // independently qualify; union evidence must not create transitive chains.
            if (!complete) continue;
            json before = json::object(), originals = json::object();
            bool manual = false;
            std::set<std::size_t> priorGroups;
            for (const auto& name : candidate.names) if (groupByName.count(name)) priorGroups.insert(groupByName.at(name));
            for (auto index : priorGroups) {
                const auto& group = existingGroups[index];
                manual = manual || group.value("source", "manual") != "automatic";
                if (group.contains("beforeMerge")) before.update(group["beforeMerge"]);
                if (group.contains("beforeEntities")) originals.update(group["beforeEntities"]);
                removedGroups.insert(index);
            }
            PlayerEntity merged; merged.entityId = *members.begin(); merged.names = candidate.names;
            std::set<IdentifierId> gameSeen;
            for (auto id : members) {
                const auto& e = s.entities.at(s.entityIndex.at(id));
                if (!merged.cloudId && e.cloudId) merged.cloudId = e.cloudId;
                auto games = json::array();
                for (auto ref : e.gameIds) {
                    games.push_back(ToUtf8(s.FindIdentifier(ref)->displayText));
                    if (gameSeen.insert(ref).second) merged.gameIds.push_back(ref);
                }
                for (const auto& name : e.names) {
                    const auto key = ToUtf8(name);
                    if (!before.contains(key)) before[key] = games;
                    if (!originals.contains(key)) { originals[key] = {{"entityId", e.entityId}}; if (e.cloudId) originals[key]["cloudId"] = *e.cloudId; }
                }
            }
            removedEntities.insert(members.begin(), members.end());
            mergedEntities.push_back(std::move(merged));
            newGroups.push_back({{"groupId", ToUtf8(candidate.groupId)}, {"source", manual ? "manual" : "automatic"}, {"names", Strings(candidate.names)},
                {"beforeMerge", before}, {"beforeEntities", originals}});
        }
        if (mergedEntities.empty()) return;
        for (std::size_t i = 0; i < existingGroups.size(); ++i) if (!removedGroups.count(i)) newGroups.push_back(existingGroups[i]);
        s.identityMetadata["groups"] = std::move(newGroups);
        s.entities.erase(std::remove_if(s.entities.begin(), s.entities.end(), [&](const auto& e) { return removedEntities.count(e.entityId) != 0; }), s.entities.end());
        for (auto& e : mergedEntities) s.entities.push_back(std::move(e));
        std::sort(s.entities.begin(), s.entities.end(), [](const auto& a, const auto& b) { return a.entityId < b.entityId; });
    }
    std::shared_ptr<Snapshot> Load() {
        Require(db != nullptr, "Library is not initialized");
        Require(Meta("storage_version") == "3" && Meta("normalizer_version") == "1", "Unsupported library storage/normalizer version");
        auto s = std::make_shared<Snapshot>(); s->persisted = true; s->revision = std::stoull(Meta("revision"));
        s->identityMetadata = json::parse(Meta("identity_metadata"));
        PurgeRetiredIdentityFields(s->identityMetadata);
        Statement entities(db, "SELECT entity_id,cloud_id FROM player_entities WHERE deleted=0 ORDER BY entity_id");
        while (entities.Step()) { PlayerEntity e; e.entityId = entities.Int(0); if (!entities.Text(1).empty()) e.cloudId = entities.Text(1); s->entityIndex[e.entityId] = s->entities.size(); s->entities.push_back(std::move(e)); }
        Statement names(db, "SELECT name,entity_id FROM player_names ORDER BY name COLLATE BINARY");
        while (names.Step()) s->entities.at(s->entityIndex.at(names.Int(1))).names.push_back(FromUtf8(names.Text(0)));
        Statement identifiers(db, "SELECT identifier_id,kind,canonical_key,display_text FROM identifiers ORDER BY identifier_id");
        while (identifiers.Step()) {
            Require(identifiers.Int(1) == 0, "Unsupported identifier kind");
            Identifier id{identifiers.Int(0), IdentifierKind::Game, FromUtf8(identifiers.Text(2)), FromUtf8(identifiers.Text(3)), {}};
            s->identifierIndex[id.identifierId] = s->identifiers.size(); s->identifiers.push_back(std::move(id));
        }
        Statement spellings(db, "SELECT identifier_id,raw_text FROM identifier_spellings ORDER BY identifier_id,raw_text");
        while (spellings.Step()) s->identifiers.at(s->identifierIndex.at(spellings.Int(0))).spellings.push_back(FromUtf8(spellings.Text(1)));
        Statement links(db, "SELECT entity_id,identifier_id FROM entity_identifiers ORDER BY entity_id,position");
        while (links.Step()) {
            auto& e = s->entities.at(s->entityIndex.at(links.Int(0))); const auto& id = s->identifiers.at(s->identifierIndex.at(links.Int(1)));
            e.gameIds.push_back(id.identifierId);
        }
        DecodeMetadata(*s); ValidateMetadata(s->identityMetadata); BuildSnapshotViews(*s); return s;
    }
    SnapshotPtr Persist(Snapshot s, Transaction& tx, bool advanceRevision = true) {
        PurgeRetiredIdentityFields(s.identityMetadata);
        ValidateMetadata(s.identityMetadata);
        SynchronizeManagedMembers(s); ValidateMetadata(s.identityMetadata);
        auto encoded = EncodeMetadata(s);
        Require(s.entities.size() <= MaxNames, "Entity count exceeds limit");
        if (advanceRevision) ++s.revision;
        s.persisted = true;
        // Build all potentially throwing projections before COMMIT. Never acknowledge an
        // in-memory mutation unless both validation and its durable transaction succeed.
        BuildSnapshotViews(s);
        Sql(db, "DELETE FROM entity_identifiers; DELETE FROM player_names; UPDATE player_entities SET deleted=1;");
        Statement entity(db, "INSERT INTO player_entities(entity_id,cloud_id,deleted) VALUES(?,NULLIF(?,''),0) ON CONFLICT(entity_id) DO UPDATE SET cloud_id=excluded.cloud_id,deleted=0");
        Statement name(db, "INSERT INTO player_names(name,entity_id) VALUES(?,?)");
        for (const auto& e : s.entities) {
            Require(!e.names.empty(), "Entity has no names"); entity.Bind(1, e.entityId); entity.Bind(2, e.cloudId.value_or("")); entity.Run();
            for (const auto& n : e.names) { ValidateText(n, true); name.Bind(1, ToUtf8(n)); name.Bind(2, e.entityId); name.Run(); }
        }
        Statement id(db, "INSERT INTO identifiers(identifier_id,kind,canonical_key,display_text) VALUES(?,?,?,?) ON CONFLICT(identifier_id) DO UPDATE SET display_text=excluded.display_text");
        Statement spelling(db, "INSERT OR IGNORE INTO identifier_spellings(identifier_id,raw_text) VALUES(?,?)");
        for (const auto& value : s.identifiers) {
            id.Bind(1, value.identifierId); id.Bind(2, 0LL); id.Bind(3, ToUtf8(value.canonicalKey)); id.Bind(4, ToUtf8(value.displayText)); id.Run();
            for (const auto& raw : value.spellings) { spelling.Bind(1, value.identifierId); spelling.Bind(2, ToUtf8(raw)); spelling.Run(); }
        }
        Statement link(db, "INSERT INTO entity_identifiers(entity_id,identifier_id,position) VALUES(?,?,?)");
        for (const auto& e : s.entities) { std::int64_t pos = 0; for (auto value : e.gameIds) { link.Bind(1, e.entityId); link.Bind(2, value); link.Bind(3, pos++); link.Run(); } }
        SetMeta("revision", std::to_string(s.revision)); SetMeta("identity_metadata", encoded.dump());
        auto result = std::make_shared<const Snapshot>(std::move(s)); tx.Commit(); return result;
    }
    SnapshotPtr UpgradeOpenDatabase() {
        Transaction tx(db); const bool migrated = MigrateSchema(); auto s = Load(); PrepareIntern(*s);
        const bool policyChanged = MigrateAutomaticPolicy(*s);
        if (migrated || policyChanged) return Persist(std::move(*s), tx);
        tx.Commit(); return s;
    }
};

PlayerLibraryDatabase::PlayerLibraryDatabase(Options options) : impl_(std::make_unique<Impl>(std::move(options))) {}
PlayerLibraryDatabase::~PlayerLibraryDatabase() = default;
const Options& PlayerLibraryDatabase::GetOptions() const noexcept { return impl_->options; }
const std::string& PlayerLibraryDatabase::GetWarning() const noexcept { return impl_->warning; }
SnapshotPtr PlayerLibraryDatabase::LoadSnapshot() {
    Require(impl_->db != nullptr, "Library is not initialized");
    Transaction tx(impl_->db, false); auto result = impl_->Load(); tx.Commit(); return result;
}
SnapshotPtr PlayerLibraryDatabase::Initialize() {
    auto& p = *impl_;
    if (p.db) return LoadSnapshot();
    if (fs::exists(p.options.databasePath)) {
        p.Open(p.options.databasePath, false);
        try {
            auto result = p.UpgradeOpenDatabase();
            p.WarnAutomaticReview(*result);
            Sql(p.db, "PRAGMA journal_mode=WAL;"); return result;
        } catch (...) { p.Close(); throw; }
    }
    if (!p.options.priorDatabasePath.empty()) {
        const auto& source = p.options.priorDatabasePath;
        if (fs::exists(source)) {
            const auto suffix = Suffix();
            const fs::path backup(p.options.databasePath.wstring() + suffix + L".prior.bak");
            const fs::path temporary(p.options.databasePath.wstring() + suffix + L".migrating");
            try {
                fs::create_directories(p.options.databasePath.parent_path());
                BackupPriorDatabase(source, backup);
                // Keep the verified pre-upgrade backup intact for recovery.
                fs::copy_file(backup, temporary);
                p.Open(temporary, false); Sql(p.db, "PRAGMA journal_mode=DELETE;");
                auto result = p.UpgradeOpenDatabase();
                CheckDatabase(p.db);
                { Transaction tx(p.db);
                    p.SetMeta("upgrade_520_prior_database", ToUtf8(source.wstring()));
                    p.SetMeta("upgrade_520_backup", ToUtf8(backup.wstring())); tx.Commit();
                }
                Backup(p.options.legacyPath, suffix); Backup(p.options.identityGroupsPath, suffix);
                p.Close();
                // No replacement flag: a concurrently installed production DB wins.
                if (!MoveFileExW(temporary.c_str(), p.options.databasePath.c_str(), MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Cannot install prior player library; destination may already exist");
                p.Open(p.options.databasePath, false); Sql(p.db, "PRAGMA journal_mode=WAL;");
                result = LoadSnapshot();
                p.WarnAutomaticReview(*result);
                if (!p.warning.empty()) p.warning += "; ";
                p.warning += "Previous player library preserved; recovery backup: " + ToUtf8(backup.wstring());
                return result;
            } catch (const std::exception& error) {
                p.Close(); std::error_code ignored; fs::remove(temporary, ignored);
                fs::remove(fs::path(temporary.wstring() + L"-journal"), ignored);
                throw std::runtime_error("Prior player library upgrade failed (source preserved: " + ToUtf8(source.wstring()) +
                    "; recovery backup, if completed: " + ToUtf8(backup.wstring()) + "): " + error.what());
            }
        }
        Require(!fs::exists(fs::path(source.wstring() + L"-wal")) && !fs::exists(fs::path(source.wstring() + L"-journal")),
            "Prior player library is missing but recovery files remain; restore the original SQLite before upgrading");
    }
    auto legacy = ParseLegacy(ReadFile(p.options.legacyPath));
    auto rawMetadata = ReadFile(p.options.identityGroupsPath);
    auto metadata = json::object();
    const auto suffix = Suffix(); Backup(p.options.legacyPath, suffix); Backup(p.options.identityGroupsPath, suffix);
    try { if (!rawMetadata.empty()) metadata = json::parse(rawMetadata); PurgeRetiredIdentityFields(metadata); ValidateMetadata(metadata); }
    catch (const std::exception& error) {
        if (!p.options.priorDatabasePath.empty()) throw std::runtime_error(std::string("Legacy identity metadata is invalid; original files and migration backups preserved: ") + error.what());
        metadata = json::object(); p.warning = "Invalid legacy identity metadata was backed up and ignored; alias library migrated successfully";
    }
    if (!p.options.priorDatabasePath.empty() && metadata.contains("groups")) {
        for (const auto& group : metadata["groups"]) for (const auto& name : ReadStrings(group["names"], true))
            Require(legacy.count(name) != 0, "Legacy identity group references a missing INI name; original files preserved, migration requires review");
    }
    fs::create_directories(p.options.databasePath.parent_path());
    fs::path temporary(p.options.databasePath.wstring() + suffix + L".migrating");
    try {
        p.Open(temporary, true); Sql(p.db, "PRAGMA journal_mode=DELETE;");
        { Transaction tx(p.db); p.Schema(); tx.Commit(); }
        auto s = ReplaceLegacy(legacy, metadata);
        Require(s->legacy.size() == legacy.size(), "Migration name count mismatch");
        // Old automatic exports copied the union to every name. Audit each union
        // once: restored snapshots remove false links without losing identifiers.
        std::unordered_map<std::wstring, std::size_t> automaticByName;
        std::vector<std::set<std::wstring>> automaticKeys;
        if (metadata.contains("groups")) for (const auto& group : metadata["groups"]) {
            if (group.value("source", "manual") != "automatic") continue;
            const auto index = automaticKeys.size(); automaticKeys.emplace_back();
            for (const auto& name : ReadStrings(group["names"], true)) if (s->legacy.count(name)) {
                automaticByName.emplace(name, index);
                const auto keys = Keys(s->legacy.at(name)); automaticKeys.back().insert(keys.begin(), keys.end());
            }
        }
        for (const auto& [name, ids] : legacy) {
            const auto actual = Keys(s->legacy.at(name));
            const auto group = automaticByName.find(name);
            for (const auto& key : Keys(ids)) Require(actual.count(key) != 0 ||
                (group != automaticByName.end() && automaticKeys[group->second].count(key) != 0), "Migration identifier verification failed");
        }
        {
            std::vector<std::wstring> rows; std::size_t links = 0;
            for (const auto& [name, ids] : legacy) { rows.push_back(name + L"=" + FormatIds(ids)); links += Keys(ids).size(); }
            Transaction tx(p.db); p.SetMeta("migration_fingerprint", IdentityFingerprint(rows));
            p.SetMeta("migration_name_count", std::to_string(legacy.size()));
            p.SetMeta("migration_reference_count", std::to_string(links)); tx.Commit();
        }
        { Statement check(p.db, "PRAGMA integrity_check"); Require(check.Step() && check.Text(0) == "ok", "Migration integrity check failed"); }
        // The temporary database never enters WAL mode and is closed before rename.
        p.Close();
        if (!MoveFileExW(temporary.c_str(), p.options.databasePath.c_str(), MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot install migrated database");
        p.Open(p.options.databasePath, false); Sql(p.db, "PRAGMA journal_mode=WAL;");
        auto result = LoadSnapshot(); p.WarnAutomaticReview(*result); return result;
    } catch (...) {
        p.Close(); std::error_code ignored; fs::remove(temporary, ignored); fs::remove(fs::path(temporary.wstring() + L"-journal"), ignored); throw;
    }
}
SnapshotPtr PlayerLibraryDatabase::LoadLegacyFallback() const {
    const auto& options = impl_->options;
    if (!options.priorDatabasePath.empty()) {
        Require(!fs::exists(options.databasePath) && !fs::exists(options.priorDatabasePath) &&
            !fs::exists(fs::path(options.priorDatabasePath.wstring() + L"-wal")) &&
            !fs::exists(fs::path(options.priorDatabasePath.wstring() + L"-journal")),
            "Authoritative SQLite/recovery files exist; refusing a weaker INI fallback");
    }
    auto library = ParseLegacy(ReadFile(impl_->options.legacyPath)); json metadata = json::object();
    try { auto raw = ReadFile(impl_->options.identityGroupsPath); if (!raw.empty()) { metadata = json::parse(raw); PurgeRetiredIdentityFields(metadata); ValidateMetadata(metadata); } } catch (...) { metadata = json::object(); }
    return BuildFallbackSnapshot(library, metadata);
}
void PlayerLibraryDatabase::ExportLegacy(const Snapshot& snapshot) {
    Require(snapshot.persisted, "Cannot export uncommitted fallback");
    if (impl_->options.legacyExportEnabled) AtomicWrite(impl_->options.legacyPath, SerializeLegacy(snapshot.legacy));
}

SnapshotPtr PlayerLibraryDatabase::ReplaceLegacy(const LegacyLibrary& library, const json& identityMetadata) {
    auto& p = *impl_; Require(p.db != nullptr, "Library is not initialized"); Transaction tx(p.db);
    auto old = p.Load(); Snapshot s = *old; p.PrepareIntern(s);
    s.identityMetadata = identityMetadata.is_null() ? old->identityMetadata : identityMetadata; PurgeRetiredIdentityFields(s.identityMetadata); ValidateMetadata(s.identityMetadata);
    Require(library.size() <= MaxNames, "Library name limit exceeded");
    LegacyLibrary input; std::size_t count = 0;
    for (const auto& [name, ids] : library) {
        ValidateText(name, true); auto clean = Trim(name); Require(!input.count(clean), "Duplicate normalized name");
        count += ids.size(); Require(count <= MaxIds, "Library identifier limit exceeded");
        for (const auto& id : ids) ValidateText(id, false);
        input.emplace(clean, ids);
    }
    // Editing a displayed automatic group is explicit confirmation of shared IDs.
    // Persist its original sets so the same edit via the flat compatibility API
    // has the same merge/split semantics as an identity command.
    for (const auto& group : old->identityState["groups"]) {
        if (group.value("source", "manual") != "automatic") continue;
        if (s.identityMetadata.contains("groups") && std::any_of(s.identityMetadata["groups"].begin(), s.identityMetadata["groups"].end(),
            [&](const auto& saved) { return saved["groupId"] == group["groupId"] && saved.contains("beforeEntities"); })) continue;
        auto names = ReadStrings(group["names"], true);
        names.erase(std::remove_if(names.begin(), names.end(), [&](const auto& name) { return !input.count(name); }), names.end());
        if (names.size() < 2) continue;
        bool changed = false;
        for (const auto& n : names) if (Keys(input.at(n)) != Keys(old->legacy.at(n))) changed = true;
        if (!changed) continue;
        if (!s.identityMetadata.contains("groups")) s.identityMetadata["groups"] = json::array();
        json before = json::object(), originals = json::object();
        for (const auto& n : names) {
            const auto key = ToUtf8(n); const auto* e = old->FindName(n); before[key] = Strings(old->legacy.at(n));
            originals[key] = {{"entityId", e->entityId}}; if (e->cloudId) originals[key]["cloudId"] = *e->cloudId;
        }
        s.identityMetadata["groups"].push_back({{"groupId", group["groupId"]}, {"source", "manual"}, {"names", Strings(names)}, {"beforeMerge", before}, {"beforeEntities", originals}});
    }
    // Explicit groups define shared identities; cloud alias sets remain shared unless
    // the caller has explicitly removed their previous local group metadata.
    std::map<std::wstring, std::wstring> parent;
    for (const auto& [name, ids] : input) parent[name] = name;
    auto root = [&](std::wstring name) { while (parent.at(name) != name) name = parent.at(name); return name; };
    auto join = [&](const std::vector<std::wstring>& names) {
        std::wstring first;
        for (const auto& n : names) if (input.count(n)) { if (first.empty()) first = n; else parent[root(n)] = root(first); }
    };
    std::set<std::wstring> priorManaged;
    if (old->identityMetadata.contains("groups")) for (const auto& g : old->identityMetadata["groups"]) for (const auto& n : ReadStrings(g["names"], true)) priorManaged.insert(n);
    for (const auto& e : old->entities) if (std::none_of(e.names.begin(), e.names.end(), [&](const auto& n) { return priorManaged.count(n) != 0; })) join(e.names);
    if (s.identityMetadata.contains("groups")) {
        auto groups = json::array();
        for (auto g : s.identityMetadata["groups"]) {
            auto names = ReadStrings(g["names"], true);
            names.erase(std::remove_if(names.begin(), names.end(), [&](const auto& n) { return !input.count(n); }), names.end());
            if (names.size() < 2) continue;
            g["names"] = Strings(names); groups.push_back(g);
            if (g.value("source", "manual") != "automatic" || g.contains("beforeEntities")) join(names);
        }
        s.identityMetadata["groups"] = std::move(groups);
    }
    std::map<std::wstring, std::vector<std::wstring>> components;
    for (const auto& [name, ids] : input) components[root(name)].push_back(name);
    s.entities.clear(); auto next = p.NextEntity(); std::set<EntityId> used;
    for (const auto& [key, names] : components) {
        PlayerEntity e; e.names = names; std::vector<std::wstring> ids, changed; bool hasChanged = false;
        for (const auto& name : names) {
            if (const auto* previous = old->FindName(name)) {
                if (!e.entityId && !used.count(previous->entityId)) { e.entityId = previous->entityId; e.cloudId = previous->cloudId; }
                if (Keys(input.at(name)) != Keys(old->legacy.at(name))) {
                    Require(!hasChanged || Keys(changed) == Keys(input.at(name)), "Conflicting edits to names in one identity group");
                    changed = input.at(name); hasChanged = true;
                }
            }
            for (const auto& id : input.at(name)) ids.push_back(id);
        }
        if (!e.entityId) e.entityId = next++;
        used.insert(e.entityId);
        e.gameIds = p.InternIds(s, hasChanged ? changed : ids, IdentifierKind::Game);
        s.entities.push_back(std::move(e));
    }
    p.MigrateAutomaticPolicy(s);
    BuildSnapshotViews(s);
    if (s.legacy == old->legacy && s.identityMetadata == old->identityMetadata && s.entities.size() == old->entities.size()) {
        // Equality of spellings is also important during migration/import auditing.
        bool same = s.identifiers.size() == old->identifiers.size();
        for (std::size_t i = 0; same && i < s.identifiers.size(); ++i) same = s.identifiers[i].spellings == old->identifiers[i].spellings;
        if (same) { tx.Commit(); return old; }
    }
    return p.Persist(std::move(s), tx);
}

SnapshotPtr PlayerLibraryDatabase::ImportV2(const json& payload, ImportReport* report) {
    if (report) *report = {};
    ImportReport outcome;
    auto& p = *impl_; Require(p.db != nullptr, "Library is not initialized");
    Require(payload.dump().size() <= MaxBytes, "Cloud payload exceeds 64 MiB");
    const json* body = &payload;
    if (body->is_object() && body->contains("data") && (*body)["data"].is_object()) body = &(*body)["data"];
    bool legacy = body->is_object() && body->contains("players");
    const bool skipConflicts = payload.is_object() && payload.value("skipOwnershipConflicts", false);
    std::optional<std::uint64_t> remoteRevision;
    std::string importSource;
    if (!legacy && body->is_object() && body->contains("revision")) {
        const auto& value = (*body)["revision"];
        Require(value.is_number_integer() && value.get<std::int64_t>() >= 0, "Remote revision must be a nonnegative integer");
        remoteRevision = value.get<std::uint64_t>();
        Require(*remoteRevision <= 9007199254740991ULL, "Remote revision exceeds the safe integer limit");
        Require(payload.is_object() && payload.contains("importSource") && payload["importSource"].is_string(), "Versioned imports require importSource");
        importSource = payload["importSource"].get<std::string>();
        Require(!importSource.empty() && importSource.size() <= 4096 && std::none_of(importSource.begin(), importSource.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }), "Invalid importSource");
        FromUtf8(importSource);
    }
    json rows;
    if (legacy) {
        Require((*body)["players"].is_object(), "Cloud players must be an object"); rows = json::array();
        for (const auto& [name, ids] : (*body)["players"].items()) {
            ValidateText(FromUtf8(name), true);
            rows.push_back({{"names", json::array({name})}, {"gameIds", Strings(ReadStrings(ids))}});
        }
    } else if (body->is_array()) rows = *body;
    else { Require(body->is_object() && body->contains("entities"), "Cloud payload is missing entities/players"); rows = (*body)["entities"]; }
    Require(rows.is_array() && rows.size() <= (legacy ? MaxNames : 10000), "Invalid cloud entity array");
    struct Incoming { std::vector<std::wstring> names, game; std::optional<std::string> cloud; };
    std::vector<Incoming> incoming; std::set<std::wstring> seenNames; std::set<std::string> seenCloud; std::size_t total = 0;
    for (const auto& row : rows) {
        Require(row.is_object() && row.contains("names"), "Cloud entity is missing names"); Incoming item;
        if (!legacy) {
            Require(row["names"].is_array() && row["names"].size() <= 10000, "Cloud names must be an array of at most 10000 strings");
            for (const auto* field : {"gameIds"}) if (row.contains(field))
                Require(row[field].is_array() && row[field].size() <= 10000, "Cloud identifiers must be arrays of at most 10000 strings");
        }
        item.names = ReadStrings(row["names"], true); Require(!item.names.empty(), "Cloud entity has no names");
        item.game = ReadStrings(row.value("gameIds", json::array()));
        if (!legacy) {
            Require(item.names.size() <= 10000 && item.game.size() <= 10000, "Cloud entity exceeds server array limits");
            for (const auto& text : item.names) ValidateCloudName(text);
            for (const auto& text : item.game) Require(text.size() <= 128, "Cloud identifier exceeds 128 UTF-16 code units");
        }
        for (const auto& n : item.names) Require(seenNames.insert(n).second, "Name occurs in multiple cloud entities");
        Require(seenNames.size() <= MaxNames, "Cloud payload name limit exceeded");
        total += item.names.size() + item.game.size(); Require(total <= MaxIds, "Cloud payload item limit exceeded");
        if (row.contains("entityId") && !row["entityId"].is_null()) {
            Require(row["entityId"].is_string(), "Cloud entityId must be a string");
            auto id = row["entityId"].get<std::string>();
            Require(ValidCloudId(id), "Invalid cloud entityId");
            Require(seenCloud.insert(id).second, "Duplicate cloud entityId"); item.cloud = id;
        }
        incoming.push_back(std::move(item));
    }
    // Redirects are authoritative only as part of a complete, valid public response.
    // Keep local constituent IDs intact so explicit split history remains reversible.
    std::map<std::string, std::string> redirects;
    if (body->is_object() && body->contains("entityRedirects")) {
        const auto& mappings = (*body)["entityRedirects"];
        Require(!legacy && mappings.is_array() && mappings.size() <= 10000, "Invalid public identity redirects");
        for (const auto& mapping : mappings) {
            Require(mapping.is_object() && mapping.contains("fromEntityId") && mapping.contains("toEntityId") &&
                mapping["fromEntityId"].is_string() && mapping["toEntityId"].is_string(), "Invalid public identity redirect");
            const auto from = mapping["fromEntityId"].get<std::string>(), to = mapping["toEntityId"].get<std::string>();
            Require(ValidCloudId(from) && ValidCloudId(to) && from != to && !seenCloud.count(from) &&
                redirects.emplace(from, to).second, "Duplicate or invalid public identity redirect");
        }
        for (auto& [from, to] : redirects) {
            std::set<std::string> path{from};
            auto target = to;
            while (redirects.count(target)) {
                Require(path.insert(target).second, "Cyclic public identity redirects");
                target = redirects.at(target);
            }
            Require(seenCloud.count(target) != 0, "Public identity redirect target is missing");
            for (const auto& step : path) redirects.at(step) = target;
        }
    }
    const auto canonicalCloud = [&](const std::string& id) { const auto it = redirects.find(id); return it == redirects.end() ? id : it->second; };
    Transaction tx(p.db); auto old = p.Load(); Snapshot s = *old; p.PrepareIntern(s); EntityId next = p.NextEntity();
    json remoteRevisions = json::object();
    if (remoteRevision) {
        const auto stored = p.Meta("remote_revisions"); if (!stored.empty()) remoteRevisions = json::parse(stored);
        Require(remoteRevisions.is_object(), "Invalid remote revision metadata");
        if (remoteRevisions.contains(importSource)) {
            const auto& accepted = remoteRevisions[importSource];
            Require(accepted.is_number_integer() && accepted.get<std::int64_t>() >= 0, "Invalid stored remote revision");
            Require(*remoteRevision >= accepted.get<std::uint64_t>(), "Stale remote library revision");
        }
    }
    std::unordered_map<std::wstring, std::size_t> byName;
    std::unordered_map<std::string, std::set<std::size_t>> byCloud;
    std::set<std::string> liveCloudIds;
    for (std::size_t i = 0; i < s.entities.size(); ++i) {
        for (const auto& name : s.entities[i].names) byName[name] = i;
        if (s.entities[i].cloudId) {
            byCloud[canonicalCloud(*s.entities[i].cloudId)].insert(i);
            liveCloudIds.insert(*s.entities[i].cloudId);
        }
    }
    // Local unions retain every remote identity in reversible constituent snapshots.
    // A live owner restored by a partial split takes precedence over its history.
    if (s.identityMetadata.contains("groups")) for (const auto& group : s.identityMetadata["groups"]) {
        if (!group.contains("beforeEntities")) continue;
        for (const auto& name : ReadStrings(group["names"], true)) {
            const auto key = ToUtf8(name);
            if (!byName.count(name) || !group["beforeEntities"].contains(key)) continue;
            const auto& original = group["beforeEntities"][key];
            if (original.contains("cloudId")) {
                const auto id = original["cloudId"].get<std::string>();
                if (!liveCloudIds.count(id)) byCloud[canonicalCloud(id)].insert(byName.at(name));
            }
        }
    }
    // Validate the whole batch before interning IDs or attaching any new names.
    std::set<std::size_t> cloudOwners;
    for (const auto& [cloud, owners] : byCloud) cloudOwners.insert(owners.begin(), owners.end());
    const auto matches = [&](const Incoming& item) {
        std::set<std::size_t> matched;
        if (item.cloud && byCloud.count(*item.cloud)) matched = byCloud.at(*item.cloud);
        for (const auto& name : item.names) if (byName.count(name)) matched.insert(byName.at(name));
        return matched;
    };
    std::map<std::size_t, std::map<std::string, std::vector<std::size_t>>> plannedCloudIds;
    std::map<std::size_t, std::string> rejected;
    for (std::size_t rowIndex = 0; rowIndex < incoming.size(); ++rowIndex) {
        const auto& item = incoming[rowIndex];
        if (!item.cloud) continue;
        for (auto index : matches(item)) {
            const bool known = byCloud.count(*item.cloud) && byCloud.at(*item.cloud).count(index);
            if (cloudOwners.count(index) && !known) {
                Require(skipConflicts, "Cloud entityId conflicts with the existing name owner");
                rejected[rowIndex] = "existing_cloud_owner";
                continue;
            }
            if (known) continue;
            plannedCloudIds[index][*item.cloud].push_back(rowIndex);
        }
    }
    for (const auto& [owner, claims] : plannedCloudIds) if (claims.size() > 1) {
        Require(skipConflicts, "Cloud batch assigns different IDs to one local entity");
        for (const auto& [cloud, claimRows] : claims) for (auto index : claimRows) rejected[index] = "multiple_cloud_assignments";
    }
    if (!rejected.empty()) {
        std::vector<Incoming> accepted;
        for (std::size_t index = 0; index < incoming.size(); ++index) {
            if (rejected.count(index)) outcome.skipped.push_back({incoming[index].cloud.value_or(""), incoming[index].names, rejected.at(index)});
            else accepted.push_back(std::move(incoming[index]));
        }
        incoming = std::move(accepted);
    }
    outcome.acceptedEntities = incoming.size();
    std::vector<const Incoming*> deferredCloudIds;
    for (const auto& item : incoming) {
        const auto matched = matches(item);
        const auto game = p.InternIds(s, item.game, IdentifierKind::Game);
        if (matched.empty()) {
            for (const auto& n : item.names) byName[n] = s.entities.size();
            if (item.cloud) byCloud[*item.cloud].insert(s.entities.size());
            PlayerEntity e; e.entityId = next++; e.cloudId = item.cloud; e.names = item.names; e.gameIds = game; s.entities.push_back(std::move(e));
        } else {
            if (matched.size() > 1 && item.cloud && !byCloud.count(*item.cloud)) deferredCloudIds.push_back(&item);
            // Add evidence first. A separate bounded pass applies the identity policy.
            for (auto index : matched) { auto& e = s.entities[index];
                for (auto id : game) AddUnique(e.gameIds, id);
                if (matched.size() == 1 && item.cloud && (!e.cloudId || e.cloudId == item.cloud)) { e.cloudId = item.cloud; byCloud[*item.cloud].insert(index); }
            }
            std::vector<std::wstring> newNames;
            for (const auto& n : item.names) {
                if (!byName.count(n)) newNames.push_back(n);
            }
            if (!newNames.empty()) {
                if (matched.size() == 1) {
                    auto& e = s.entities[*matched.begin()]; for (const auto& n : newNames) { e.names.push_back(n); byName[n] = *matched.begin(); }
                } else { for (const auto& n : newNames) byName[n] = s.entities.size(); PlayerEntity e; e.entityId = next++; e.names = newNames; e.gameIds = game; s.entities.push_back(std::move(e)); }
            }
        }
    }
    Require(byName.size() <= MaxNames, "Library name limit exceeded");
    // A wholly rejected pull must not run unrelated local identity normalization.
    if (!incoming.empty() || outcome.skipped.empty()) {
        SynchronizeManagedMembers(s);
        p.CoalesceAutomatic(s);
    }
    if (!deferredCloudIds.empty() && s.identityMetadata.contains("groups")) {
        byName.clear();
        for (std::size_t i = 0; i < s.entities.size(); ++i) for (const auto& name : s.entities[i].names) byName.emplace(name, i);
        std::unordered_map<std::wstring, std::size_t> groupByName;
        auto& groups = s.identityMetadata["groups"];
        for (std::size_t i = 0; i < groups.size(); ++i) for (const auto& name : ReadStrings(groups[i]["names"], true)) groupByName.emplace(name, i);
        std::map<std::pair<std::size_t, EntityId>, std::string> originalCloudIds;
        for (const auto* item : deferredCloudIds) {
            const auto& anchor = *std::min_element(item->names.begin(), item->names.end());
            const auto owner = byName.at(anchor);
            if (!std::all_of(item->names.begin(), item->names.end(), [&](const auto& name) { return byName.at(name) == owner; })) continue;
            // Only a proven physical union resolves an ambiguous first pull. Keep
            // one deterministic constituent's cloud reference for a later split.
            auto& e = s.entities[owner];
            if (!e.cloudId) e.cloudId = item->cloud;
            const auto groupIndex = groupByName.at(anchor);
            const auto source = groups[groupIndex]["beforeEntities"].at(ToUtf8(anchor)).at("entityId").get<EntityId>();
            const auto assigned = originalCloudIds.emplace(std::make_pair(groupIndex, source), *item->cloud);
            Require(assigned.second || assigned.first->second == *item->cloud, "Cloud batch assigns different IDs to one original entity");
        }
        for (std::size_t i = 0; i < groups.size(); ++i) {
            if (!groups[i].contains("beforeEntities")) continue;
            for (auto& [name, original] : groups[i]["beforeEntities"].items()) {
                if (!original.contains("entityId")) continue;
                const auto assigned = originalCloudIds.find({i, original["entityId"].get<EntityId>()});
                if (assigned == originalCloudIds.end()) continue;
                Require(!original.contains("cloudId") || original["cloudId"] == assigned->second, "Cloud entityId conflicts with the original name owner");
                original["cloudId"] = assigned->second;
            }
        }
    }
    const bool unchanged = SameNormalizedState(s, *old);
    if (remoteRevision) {
        remoteRevisions[importSource] = *remoteRevision;
        p.SetMeta("remote_revisions", remoteRevisions.dump());
    }
    SnapshotPtr result;
    if (unchanged && SameIdentifierStorage(s, *old)) { tx.Commit(); result = old; }
    else result = p.Persist(std::move(s), tx, !unchanged);
    if (report) *report = std::move(outcome);
    return result;
}

SnapshotPtr PlayerLibraryDatabase::ResetLocal(const json& command) {
    auto& p = *impl_;
    Require(command.value("confirmed", false), "Local reset requires explicit confirmation");
    const auto old = LoadSnapshot();
    Require(command.contains("revision") && command["revision"].is_number_unsigned() &&
        command["revision"].get<std::uint64_t>() == old->revision, "Stale library revision; refresh and retry");
    const fs::path folder = p.options.databasePath.parent_path() / (L"library-reset" + Suffix());
    fs::create_directory(folder);
    sqlite3* backupDb = nullptr;
    const auto backupFile = ToUtf8((folder / L"player_library.db").wstring());
    if (sqlite3_open(backupFile.c_str(), &backupDb) != SQLITE_OK) {
        if (backupDb) sqlite3_close(backupDb);
        throw std::runtime_error("Cannot create local library backup");
    }
    sqlite3_backup* backup = sqlite3_backup_init(backupDb, "main", p.db, "main");
    const int copied = backup ? sqlite3_backup_step(backup, -1) : SQLITE_ERROR;
    const int finished = backup ? sqlite3_backup_finish(backup) : SQLITE_ERROR;
    sqlite3_close(backupDb);
    Require(copied == SQLITE_DONE && finished == SQLITE_OK, "Local library backup failed; nothing deleted");
    struct Original { fs::path path; std::string contents; bool existed; };
    std::vector<Original> originals;
    for (const auto& path : {p.options.legacyPath, p.options.identityGroupsPath,
        p.options.legacyPath.parent_path() / L"alias_cloud_baseline.json",
        p.options.legacyPath.parent_path() / L"players_config.txt"}) {
        Require(!fs::is_symlink(path), "Refusing to reset a linked library file");
        originals.push_back({path, ReadFile(path), fs::exists(path)});
        if (originals.back().existed) fs::copy_file(path, folder / path.filename());
    }
    // Restore only files this attempt replaced, while retaining the SQLite writer lock.
    std::size_t replaced = 0;
    Transaction tx(p.db);
    try {
        Require(p.Load()->revision == old->revision, "Library changed during reset backup");
        const std::string clearedFiles[] = {SerializeLegacy({}),
            "{\"version\":1,\"groups\":[],\"autoSplitFingerprints\":[]}", "{\"mainNames\":[],\"players\":{}}", ""};
        for (const auto& contents : clearedFiles) {
            AtomicWrite(originals[replaced].path, contents, "Local reset file replacement failed");
            ++replaced;
        }
        Sql(p.db, "DELETE FROM entity_identifiers; DELETE FROM player_names; DELETE FROM identifier_spellings; DELETE FROM identifiers; DELETE FROM player_entities;");
        Sql(p.db, "DELETE FROM library_meta WHERE key GLOB 'migration_*';");
        p.SetMeta("remote_revisions", "{}");
        Snapshot empty; empty.revision = old->revision;
        empty.identityMetadata = {{"localResetBackup", ToUtf8(folder.wstring())},
            {"autoGroupPolicyVersion", dnf::identity::AUTO_GROUP_POLICY_VERSION}};
        auto result = p.Persist(std::move(empty), tx);
        p.PrepareIntern(*result);
        return result;
    } catch (...) {
        while (replaced > 0) {
            const auto& original = originals[--replaced];
            try {
                if (original.existed) AtomicWrite(original.path, original.contents);
                else { std::error_code ignored; fs::remove(original.path, ignored); }
            } catch (...) { /* The SQLite backup and original files remain in folder. */ }
        }
        throw;
    }
}

SnapshotPtr PlayerLibraryDatabase::ExecuteIdentity(const json& command) {
    Require(command.is_object(), "Identity command must be an object");
    std::string action = command.value("action", command.value("command", std::string()));
    const std::string prefix = "cmd_identity_"; if (action.compare(0, prefix.size(), prefix) == 0) action.erase(0, prefix.size());
    if (action == "refresh") return LoadSnapshot();
    if (action == "reset_local") return ResetLocal(command);
    auto& p = *impl_; Require(p.db != nullptr, "Library is not initialized");
    Require(command.dump().size() <= MaxBytes, "Identity command exceeds size limit");
    Transaction tx(p.db); auto old = p.Load(); Snapshot s = *old; p.PrepareIntern(s); auto next = p.NextEntity();
    Require(command.contains("revision") && command["revision"].is_number_integer(), "Identity command requires revision");
    Require(command["revision"].get<std::int64_t>() >= 0 && command["revision"].get<std::uint64_t>() == s.revision, "Stale library revision; refresh and retry");
    if (!s.identityMetadata.contains("groups")) s.identityMetadata["groups"] = json::array();
    if (!s.identityMetadata.contains("autoSplitFingerprints")) s.identityMetadata["autoSplitFingerprints"] = json::array();
    SynchronizeManagedMembers(s);
    for (const auto& g : old->identityState["groups"]) {
        if (g.value("source", "automatic") == "automatic") continue;
        bool exists = false; for (const auto& stored : s.identityMetadata["groups"]) if (stored["groupId"] == g["groupId"]) exists = true;
        if (exists) continue;
        const auto names = ReadStrings(g["names"], true);
        json before = json::object(), originals = json::object();
        bool first = true;
        for (const auto& n : names) {
            auto key = ToUtf8(n); const auto* e = old->FindName(n); before[key] = Strings(old->legacy.at(n));
            if (first) { originals[key] = {{"entityId", e->entityId}}; if (e->cloudId) originals[key]["cloudId"] = *e->cloudId; first = false; }
        }
        s.identityMetadata["groups"].push_back({{"groupId", g["groupId"]}, {"source", "manual"}, {"names", g["names"]}, {"beforeMerge", before}, {"beforeEntities", originals}});
    }
    auto field = [&](const char* name) { if (!command.contains(name)) return std::wstring(); Require(command[name].is_string(), "Command field must be a string"); return Trim(FromUtf8(command[name].get<std::string>())); };
    auto entity = [&](EntityId id) -> PlayerEntity& { for (auto& e : s.entities) if (e.entityId == id) return e; throw std::invalid_argument("Player entity no longer exists"); };
    auto nameEntity = [&](const std::wstring& name) -> EntityId { for (const auto& e : s.entities) if (std::find(e.names.begin(), e.names.end(), name) != e.names.end()) return e.entityId; throw std::invalid_argument("Player name no longer exists"); };
    auto displays = [&](const std::vector<IdentifierId>& refs) {
        std::vector<std::wstring> result;
        for (auto ref : refs) { bool found = false; for (const auto& id : s.identifiers) if (id.identifierId == ref) { result.push_back(id.displayText); found = true; break; } Require(found, "Missing identifier"); }
        return result;
    };
    std::set<std::string> ignoredFingerprints;
    for (const auto& value : s.identityMetadata["autoSplitFingerprints"])
        ignoredFingerprints.insert(value.get<std::string>());
    auto addIgnored = [&](const std::vector<std::wstring>& names) {
        const auto fingerprint = IdentityFingerprint(names); auto& ignored = s.identityMetadata["autoSplitFingerprints"];
        if (ignoredFingerprints.insert(fingerprint).second) ignored.push_back(fingerprint);
    };
    auto addSplitException = [&](std::vector<std::wstring> names) {
        std::sort(names.begin(), names.end());
        addIgnored(names);
        if (!s.identityMetadata.contains("autoSplitNameSets")) s.identityMetadata["autoSplitNameSets"] = json::array();
        auto& sets = s.identityMetadata["autoSplitNameSets"];
        const auto values = Strings(names);
        if (std::find(sets.begin(), sets.end(), values) == sets.end()) sets.push_back(values);
    };
    const auto groupId = ToUtf8(field("groupId"));
    auto groupNames = [&]() {
        for (const auto& g : old->identityState["groups"]) if (g["groupId"] == groupId) return ReadStrings(g["names"], true);
        throw std::invalid_argument("Identity group no longer exists");
    };
    auto merge = [&](std::vector<std::wstring> names) -> EntityId {
        Require(names.size() >= 2, "Merge requires at least two names");
        std::set<EntityId> members;
        for (const auto& n : names) members.insert(nameEntity(n));
        for (auto id : members) for (const auto& n : entity(id).names) AddUnique(names, n);
        std::sort(names.begin(), names.end());
        if (members.size() == 1) for (const auto& g : s.identityMetadata["groups"]) {
            auto existing = ReadStrings(g["names"], true); std::sort(existing.begin(), existing.end());
            if (existing == names) return *members.begin();
        }
        json before = json::object(), originals = json::object();
        std::set<std::wstring> addedAliases;
        auto groups = json::array();
        for (const auto& g : s.identityMetadata["groups"]) {
            const auto gn = ReadStrings(g["names"], true);
            const bool intersects = std::any_of(gn.begin(), gn.end(), [&](const auto& n) { return std::find(names.begin(), names.end(), n) != names.end(); });
            if (!intersects) { groups.push_back(g); continue; }
            if (g.contains("beforeMerge")) for (const auto& n : gn) if (!g["beforeMerge"].contains(ToUtf8(n))) addedAliases.insert(n);
            if (g.contains("beforeMerge")) before.update(g["beforeMerge"]);
            if (g.contains("beforeEntities")) originals.update(g["beforeEntities"]);
        }
        for (const auto& name : names) {
            const auto& e = entity(nameEntity(name)); const auto key = ToUtf8(name);
            if (addedAliases.count(name)) continue;
            if (!before.contains(key)) before[key] = Strings(displays(e.gameIds));
            if (!originals.contains(key)) { originals[key] = {{"entityId", e.entityId}}; if (e.cloudId) originals[key]["cloudId"] = *e.cloudId; }
        }
        PlayerEntity merged = entity(*members.begin()); merged.names = names;
        for (auto id : members) for (auto ref : entity(id).gameIds) AddUnique(merged.gameIds, ref);
        const auto target = merged.entityId;
        s.entities.erase(std::remove_if(s.entities.begin(), s.entities.end(), [&](const auto& e) { return members.count(e.entityId) != 0; }), s.entities.end()); s.entities.push_back(std::move(merged));
        groups.push_back({{"groupId", "identity-" + IdentityFingerprint(names)}, {"source", "manual"}, {"names", Strings(names)}, {"beforeMerge", before}, {"beforeEntities", originals}});
        s.identityMetadata["groups"] = std::move(groups);
        return target;
    };
    auto split = [&](const std::vector<std::wstring>& requested, bool all) {
        std::size_t groupIndex = 0; bool found = false;
        for (const auto& g : s.identityMetadata["groups"]) { if (g["groupId"] == groupId) { found = true; break; } ++groupIndex; }
        if (!found) {
            const auto names = groupNames();
            Require(all || !requested.empty(), "Split requires names or splitAll");
            for (const auto& n : requested) Require(std::find(names.begin(), names.end(), n) != names.end(), "Split name is not a group member");
            addSplitException(names);
            return;
        }
        auto group = s.identityMetadata["groups"][groupIndex];
        auto names = ReadStrings(group["names"], true);
        Require(!names.empty(), "Identity group has no names");
        const auto original = entity(nameEntity(names.front()));
        for (const auto& actual : original.names) AddUnique(names, actual);
        for (const auto& name : names) Require(nameEntity(name) == original.entityId, "Split group spans multiple current entities");
        auto selected = all ? names : requested;
        Require(!selected.empty(), "Split requires names or splitAll");
        for (const auto& n : selected) Require(std::find(names.begin(), names.end(), n) != names.end(), "Split name is not a group member");
        const auto before = group.value("beforeMerge", json::object());
        std::set<IdentifierId> knownGame;
        for (const auto& n : names) {
            const auto key = ToUtf8(n);
            for (auto id : p.InternIds(s, ReadStrings(before.value(key, json::array())), IdentifierKind::Game)) knownGame.insert(id);
        }
        std::vector<IdentifierId> extraGame;
        for (auto id : original.gameIds) if (!knownGame.count(id)) extraGame.push_back(id);
        const auto keep = field("keepNewIdsWith");
        if (!extraGame.empty()) Require(std::find(names.begin(), names.end(), keep) != names.end(), "Split has new IDs; provide keepNewIdsWith member name");
        std::vector<std::wstring> remaining;
        for (const auto& n : names) if (std::find(selected.begin(), selected.end(), n) == selected.end()) remaining.push_back(n);
        std::vector<PlayerEntity> restored;
        const auto originals = group.value("beforeEntities", json::object()); std::map<EntityId, std::size_t> restoredIds;
        std::map<EntityId, EntityId> originalTargets;
        for (const auto& n : selected) {
            const auto key = ToUtf8(n); EntityId id = next++;
            if (originals.contains(key) && originals[key].contains("entityId")) {
                const auto source = originals[key]["entityId"].get<EntityId>();
                const auto target = originalTargets.find(source);
                if (target != originalTargets.end()) id = target->second;
                else {
                    id = source;
                    if ((!remaining.empty() && id == original.entityId) || std::any_of(s.entities.begin(), s.entities.end(), [&](const auto& e) { return e.entityId == id && e.entityId != original.entityId; })) id = next++;
                    originalTargets.emplace(source, id);
                }
            }
            PlayerEntity e; e.entityId = id; e.names = {n};
            // Names added after merge inherit IDs on split; original names restore snapshots.
            e.gameIds = before.contains(key) ? p.InternIds(s, ReadStrings(before[key]), IdentifierKind::Game) : original.gameIds;
            if (originals.contains(key) && originals[key].contains("cloudId")) e.cloudId = originals[key]["cloudId"].get<std::string>();
            if (n == keep) { for (auto ref : extraGame) AddUnique(e.gameIds, ref); }
            else {
                e.gameIds.erase(std::remove_if(e.gameIds.begin(), e.gameIds.end(), [&](auto ref) { return std::find(extraGame.begin(), extraGame.end(), ref) != extraGame.end(); }), e.gameIds.end());
            }
            auto existing = restoredIds.find(id);
            if (existing == restoredIds.end()) { restoredIds[id] = restored.size(); restored.push_back(std::move(e)); }
            else { auto& target = restored[existing->second]; target.names.push_back(n); for (auto ref : e.gameIds) AddUnique(target.gameIds, ref); }
        }
        s.entities.erase(std::remove_if(s.entities.begin(), s.entities.end(), [&](const auto& e) { return e.entityId == original.entityId; }), s.entities.end());
        if (!remaining.empty()) {
            auto left = original; left.names = remaining; left.cloudId.reset();
            left.gameIds.clear();
            for (const auto& n : remaining) {
                const auto key = ToUtf8(n);
                const auto game = before.contains(key) ? p.InternIds(s, ReadStrings(before[key]), IdentifierKind::Game) : original.gameIds;
                for (auto ref : game) if (!std::count(extraGame.begin(), extraGame.end(), ref)) AddUnique(left.gameIds, ref);
                if (!left.cloudId && originals.contains(key) && originals[key].contains("cloudId")) left.cloudId = originals[key]["cloudId"].get<std::string>();
            }
            if (std::find(remaining.begin(), remaining.end(), keep) != remaining.end()) {
                for (auto ref : extraGame) AddUnique(left.gameIds, ref);
            }
            for (const auto& e : restored) if (e.cloudId && e.cloudId == left.cloudId) left.cloudId.reset();
            s.entities.push_back(std::move(left));
        }
        for (auto& e : restored) s.entities.push_back(std::move(e));
        s.identityMetadata["groups"].erase(groupIndex);
        if (remaining.size() >= 2) { group["names"] = Strings(remaining); s.identityMetadata["groups"].push_back(group); }
        addSplitException(names);
    };

    if (action == "create_player") {
        const auto name = field("name"); ValidateText(name, true);
        Require(!old->FindName(name), "Player name already exists");
        Require(command.contains("ids") && command["ids"].is_array(), "Player creation requires ids array");
        const auto game = ReadStrings(command["ids"]);
        PlayerEntity created; created.entityId = next++; created.names = {name};
        created.gameIds = p.InternIds(s, game, IdentifierKind::Game);
        s.entities.push_back(std::move(created));
    } else if (action == "rename_name") {
        const auto name = field("name"), replacement = field("newName"); ValidateText(replacement, true);
        const auto id = nameEntity(name);
        if (name == replacement) { tx.Commit(); return old; }
        Require(!old->FindName(replacement), "Player name already exists");
        const auto oldKey = ToUtf8(name), newKey = ToUtf8(replacement);
        if (s.identityMetadata.contains("autoSplitNameSets")) for (auto& names : s.identityMetadata["autoSplitNameSets"]) {
            for (auto& member : names) if (member == oldKey) member = newKey;
            std::sort(names.begin(), names.end());
        }
        for (auto& group : s.identityMetadata["groups"]) {
            auto names = ReadStrings(group["names"], true);
            for (auto& member : names) if (member == name) member = replacement;
            group["names"] = Strings(names);
            for (const auto* fieldName : {"beforeMerge", "beforeEntities"}) {
                if (!group.contains(fieldName) || !group[fieldName].contains(oldKey)) continue;
                auto& saved = group[fieldName]; Require(!saved.contains(newKey), "Rename conflicts with an existing split snapshot");
                auto value = saved[oldKey]; saved.erase(oldKey); saved[newKey] = std::move(value);
            }
        }
        // Preserve legacy pair ignores across an explicit rename without guessing
        // that arbitrary one-name removals/additions represent the same player.
        auto ignored = s.identityMetadata["autoSplitFingerprints"];
        for (const auto& [other, ids] : old->legacy) if (other != name) {
            const auto fingerprint = IdentityFingerprint({name, other});
            if (std::find(ignored.begin(), ignored.end(), json(fingerprint)) != ignored.end()) addIgnored({replacement, other});
        }
        for (const auto& group : old->identityMetadata.value("groups", json::array())) {
            auto names = ReadStrings(group["names"], true);
            if (std::find(ignored.begin(), ignored.end(), json(IdentityFingerprint(names))) == ignored.end()) continue;
            for (auto& member : names) if (member == name) member = replacement;
            addIgnored(names);
        }
        for (auto& member : entity(id).names) if (member == name) member = replacement;
    } else if (action == "rename_id") {
        const auto name = field("name"), previous = field("oldId"), replacement = field("newId");
        ValidateText(previous, false); ValidateText(replacement, false);
        EntityId target = nameEntity(name);
        for (const auto& group : old->identityState["groups"]) {
            const auto names = ReadStrings(group["names"], true);
            if (std::find(names.begin(), names.end(), name) != names.end()) { target = merge(names); break; }
        }
        const auto key = CanonicalKey(previous, IdentifierKind::Game);
        IdentifierId oldId = 0;
        for (auto id : entity(target).gameIds) {
            const auto* value = old->FindIdentifier(id);
            if (value && value->canonicalKey == key) { oldId = id; break; }
        }
        Require(oldId != 0, "Game ID no longer exists in the target identity");
        const auto newId = p.Intern(s, replacement, IdentifierKind::Game);
        std::vector<IdentifierId> renamed;
        for (auto id : entity(target).gameIds) AddUnique(renamed, id == oldId ? newId : id);
        entity(target).gameIds = std::move(renamed);
    } else if (action == "merge") {
        Require(command.contains("names"), "Merge requires names"); merge(ReadStrings(command["names"], true));
    } else if (action == "add_alias") {
        auto alias = field("newName"); if (alias.empty()) alias = field("alias"); ValidateText(alias, true);
        Require(!old->FindName(alias), "Alias already exists");
        auto source = field("sourceName"); if (source.empty()) source = field("name");
        EntityId id;
        if (!groupId.empty()) { auto names = groupNames(); id = names.size() > 1 ? merge(names) : nameEntity(names.front()); }
        else id = nameEntity(source);
        auto& e = entity(id); const auto oldNames = e.names; e.names.push_back(alias);
        bool managed = false;
        for (auto& g : s.identityMetadata["groups"]) {
            auto names = ReadStrings(g["names"], true);
            if (std::find(names.begin(), names.end(), oldNames.front()) != names.end()) { g["names"].push_back(ToUtf8(alias)); managed = true; break; }
        }
        if (!managed) {
            json before = json::object(), originals = json::object();
            for (const auto& n : oldNames) { auto key = ToUtf8(n); before[key] = Strings(displays(e.gameIds)); originals[key] = {{"entityId", e.entityId}}; if (e.cloudId) originals[key]["cloudId"] = *e.cloudId; }
            s.identityMetadata["groups"].push_back({{"groupId", "identity-" + IdentityFingerprint(e.names)}, {"source", "manual"}, {"names", Strings(e.names)}, {"beforeMerge", before}, {"beforeEntities", originals}});
        }
    } else if (action == "update_ids") {
        const auto kind = IdentifierKind::Game;
        const char* inputField = "ids";
        Require(command.contains(inputField), "Identifier update requires ids");
        auto values = ReadStrings(command[inputField]);
        EntityId target;
        if (!groupId.empty()) { auto names = groupNames(); target = merge(names); }
        else target = nameEntity(field("name"));
        auto refs = p.InternIds(s, values, kind);
        entity(target).gameIds = std::move(refs);
    } else if (action == "unmerge") {
        Require(!groupId.empty(), "Unmerge requires groupId");
        split(command.contains("names") ? ReadStrings(command["names"], true) : std::vector<std::wstring>(), command.value("splitAll", false));
    } else if (action == "delete_alias") {
        const auto name = field("name"); const auto id = nameEntity(name); bool originalName = false;
        if (!groupId.empty()) {
            for (const auto& g : s.identityMetadata["groups"]) if (g["groupId"] == groupId) {
                const auto names = ReadStrings(g["names"], true); Require(std::find(names.begin(), names.end(), name) != names.end(), "Alias is not a group member");
                originalName = g.contains("beforeMerge") && g["beforeMerge"].contains(ToUtf8(name));
            }
            bool manual = false; for (const auto& g : s.identityMetadata["groups"]) if (g["groupId"] == groupId) manual = true;
            if (originalName || !manual) { split({name}, false); return p.Persist(std::move(s), tx); }
        }
        auto& e = entity(id); e.names.erase(std::remove(e.names.begin(), e.names.end(), name), e.names.end());
        if (e.names.empty()) s.entities.erase(std::remove_if(s.entities.begin(), s.entities.end(), [&](const auto& v) { return v.entityId == id; }), s.entities.end());
        auto groups = json::array();
        for (auto g : s.identityMetadata["groups"]) { auto names = ReadStrings(g["names"], true); names.erase(std::remove(names.begin(), names.end(), name), names.end()); if (names.size() >= 2) { g["names"] = Strings(names); groups.push_back(g); } }
        s.identityMetadata["groups"] = std::move(groups);
    } else if (action == "ignore_overlaps") {
        Require(command.contains("pairs") && command["pairs"].is_array() && !command["pairs"].empty(), "Bulk ignore requires non-empty pairs");
        using NamePair = std::pair<std::wstring, std::wstring>;
        std::set<NamePair> current, requested;
        for (const auto& suggestion : old->identityState["overlapSuggestions"]) {
            const auto left = FromUtf8(suggestion["leftName"].get<std::string>());
            const auto right = FromUtf8(suggestion["rightName"].get<std::string>());
            current.emplace((std::min)(left, right), (std::max)(left, right));
        }
        // Validate every supplied row against this revision before appending any
        // exceptions. Unselected (including hidden or newly discovered) rows stay.
        for (const auto& pair : command["pairs"]) {
            Require(pair.is_object() && pair.contains("leftName") && pair["leftName"].is_string() &&
                pair.contains("rightName") && pair["rightName"].is_string(), "Invalid overlap pair");
            const auto left = Trim(FromUtf8(pair["leftName"].get<std::string>()));
            const auto right = Trim(FromUtf8(pair["rightName"].get<std::string>()));
            ValidateText(left, true); ValidateText(right, true);
            Require(left != right, "Cannot ignore self overlap");
            const NamePair names{(std::min)(left, right), (std::max)(left, right)};
            Require(current.count(names) != 0, "Overlap suggestion is no longer valid");
            requested.insert(names);
        }
        for (const auto& names : requested) addIgnored({names.first, names.second});
    } else if (action == "ignore_overlap") {
        const auto left = field("leftName"), right = field("rightName"); Require(left != right, "Cannot ignore self overlap");
        const auto& a = entity(nameEntity(left)); const auto& b = entity(nameEntity(right));
        Require(std::any_of(a.gameIds.begin(), a.gameIds.end(), [&](auto id) { return std::find(b.gameIds.begin(), b.gameIds.end(), id) != b.gameIds.end(); }), "Overlap suggestion is no longer valid");
        addIgnored({left, right});
    } else throw std::invalid_argument("Unknown identity command");
    return p.Persist(std::move(s), tx);
}
} // namespace dnf::player_library
