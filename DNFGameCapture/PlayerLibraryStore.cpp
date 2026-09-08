#include "PlayerLibraryStore.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace dnf::player_library {
struct PlayerLibraryStore::State {
    struct Command { RequestId id; int kind; LegacyLibrary library; nlohmann::json payload; };
    Options options;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Command> commands;
    std::vector<Result> results;
    SnapshotPtr published = std::make_shared<const Snapshot>();
    RequestId next = 1;
    bool closing = false;
    std::atomic<bool> stopped{false};
    explicit State(Options value) : options(std::move(value)) {}
    static void Run(std::shared_ptr<State> state) noexcept {
        std::unique_ptr<PlayerLibraryDatabase> database;
        for (;;) {
            Command command;
            {
                std::unique_lock<std::mutex> lock(state->mutex);
                state->wake.wait(lock, [&] { return state->closing || !state->commands.empty(); });
                if (state->commands.empty() && state->closing) break;
                command = std::move(state->commands.front()); state->commands.pop_front();
            }
            Result result; result.requestId = command.id;
            try {
                if (!database) database = std::make_unique<PlayerLibraryDatabase>(state->options);
                SnapshotPtr snapshot;
                switch (command.kind) {
                case 0: snapshot = database->Initialize(); break;
                case 1: snapshot = database->ReplaceLegacy(command.library, command.payload); break;
                case 2: snapshot = database->ImportV2(command.payload, &result.importReport); break;
                case 3: snapshot = database->ExecuteIdentity(command.payload); break;
                default: snapshot = database->LoadSnapshot(); break;
                }
                std::atomic_store(&state->published, snapshot);
                result.ok = true; result.snapshot = snapshot;
                if (command.kind == 3) result.command = command.payload;
                result.message = command.kind == 0 && !database->GetWarning().empty() ? database->GetWarning() : "Player library committed";
                try { database->ExportLegacy(*snapshot); } catch (const std::exception& error) { result.message = error.what(); }
            } catch (const std::exception& error) {
                result.error = error.what(); result.snapshot = std::atomic_load(&state->published);
                if (command.kind == 0 && !result.snapshot->persisted) {
                    try {
                        if (database) result.snapshot = database->LoadLegacyFallback();
                        std::atomic_store(&state->published, result.snapshot);
                    } catch (const std::exception& fallbackError) { result.error += std::string("; fallback: ") + fallbackError.what(); }
                }
            } catch (...) { result.error = "Unexpected player library worker failure"; result.snapshot = std::atomic_load(&state->published); }
            {
                std::lock_guard<std::mutex> lock(state->mutex); state->results.push_back(std::move(result));
            }
        }
        database.reset(); state->stopped.store(true, std::memory_order_release);
    }
};
PlayerLibraryStore::PlayerLibraryStore(Options options) : state_(std::make_shared<State>(std::move(options))) {
    std::thread(State::Run, state_).detach();
}
PlayerLibraryStore::~PlayerLibraryStore() { Shutdown(); }
RequestId PlayerLibraryStore::Enqueue(int kind, LegacyLibrary library, nlohmann::json payload) {
    std::lock_guard<std::mutex> lock(state_->mutex); const auto id = state_->next++;
    if (state_->closing) state_->results.push_back({id, false, "Player library is shutting down", "", std::atomic_load(&state_->published)});
    else { state_->commands.push_back({id, kind, std::move(library), std::move(payload)}); state_->wake.notify_one(); }
    return id;
}
RequestId PlayerLibraryStore::Initialize() { return Enqueue(0, {}, nullptr); }
RequestId PlayerLibraryStore::SubmitLegacy(LegacyLibrary library, nlohmann::json metadata) { return Enqueue(1, std::move(library), std::move(metadata)); }
RequestId PlayerLibraryStore::ImportV2(nlohmann::json payload) { return Enqueue(2, {}, std::move(payload)); }
RequestId PlayerLibraryStore::ExecuteIdentity(nlohmann::json command) { return Enqueue(3, {}, std::move(command)); }
RequestId PlayerLibraryStore::RequestRefresh() { return Enqueue(4, {}, nullptr); }
SnapshotPtr PlayerLibraryStore::GetSnapshot() const noexcept { return std::atomic_load(&state_->published); }
std::vector<Result> PlayerLibraryStore::Drain() { std::lock_guard<std::mutex> lock(state_->mutex); std::vector<Result> results; results.swap(state_->results); return results; }
void PlayerLibraryStore::Shutdown() noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex); if (state_->closing) return; state_->closing = true;
    for (const auto& command : state_->commands) state_->results.push_back({command.id, false, "Player library request cancelled by shutdown", "", std::atomic_load(&state_->published)});
    state_->commands.clear(); state_->wake.notify_all();
}
bool PlayerLibraryStore::IsStopped() const noexcept { return state_->stopped.load(std::memory_order_acquire); }
} // namespace dnf::player_library
