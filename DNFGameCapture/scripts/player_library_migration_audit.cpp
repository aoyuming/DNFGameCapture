#include "PlayerLibraryDatabase.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace dnf::player_library;
std::string Read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot read audit input");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 3) throw std::runtime_error("Usage: audit legacy-source temporary-directory [identity-metadata]");
        const fs::path source = argv[1], temp = argv[2];
        const auto original = Read(source);
        const auto legacy = ParseLegacy(original);
        fs::create_directories(temp);
        const auto copy = temp / L"alias_db.ini";
        fs::copy_file(source, copy);
        const auto metadata = temp / L"player_identity_groups.json";
        if (argc > 3 && fs::exists(argv[3])) fs::copy_file(argv[3], metadata);
        const auto started = std::chrono::steady_clock::now();
        PlayerLibraryDatabase database({temp / L"player_library.db", copy, metadata, true});
        auto snapshot = database.Initialize();
        std::size_t occurrences = 0;
        std::set<std::wstring> keys;
        if (legacy.size() != snapshot->legacy.size()) throw std::runtime_error("Player names lost during migration");
        for (const auto& [name, ids] : legacy) {
            std::set<std::wstring> migrated;
            for (const auto& id : snapshot->legacy.at(name)) migrated.insert(CanonicalKey(id, IdentifierKind::Game));
            for (const auto& id : ids) {
                ++occurrences;
                const auto key = CanonicalKey(id, IdentifierKind::Game);
                keys.insert(key);
                if (!migrated.count(key)) throw std::runtime_error("Identifier lost during migration");
            }
        }
        for (const auto& identifier : snapshot->identifiers) {
            if (identifier.kind != IdentifierKind::Game) continue;
            std::set<EntityId> expected;
            for (const auto& entity : snapshot->entities) {
                for (auto id : entity.gameIds) if (id == identifier.identifierId) expected.insert(entity.entityId);
            }
            const auto actual = snapshot->Lookup(identifier.displayText).candidates;
            if (std::set<EntityId>(actual.begin(), actual.end()) != expected) throw std::runtime_error("Index differs from reference scan");
        }
        database.ExportLegacy(*snapshot);
        auto reopened = database.LoadSnapshot();
        if (reopened->legacy != snapshot->legacy) throw std::runtime_error("Readback differs");
        if (Read(source) != original) throw std::runtime_error("Original input modified");
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        std::cout << "Migration audit passed: names=" << legacy.size() << ", ID occurrences=" << occurrences
            << ", unique IDs=" << keys.size() << ", stored identifiers=" << snapshot->identifiers.size()
            << ", entities=" << snapshot->entities.size() << ", elapsed_ms=" << ms << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
