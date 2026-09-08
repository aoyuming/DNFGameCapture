#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace dnf::ocr {
enum class ProbeResult { Ready, Busy, Failed };
struct HealthDecision { bool ready = false; bool restart = false; };
class HealthMonitor {
public:
    void BeginProcessStart(std::uint64_t now) {
        ready_ = false; failedSince_ = now; failures_ = 0;
    }
    HealthDecision Observe(std::uint64_t now, bool processAlive, ProbeResult probe,
        std::uint64_t lastRequestSuccess) {
        const bool newSuccess = lastRequestSuccess > lastSuccess_;
        if (newSuccess) lastSuccess_ = lastRequestSuccess;
        if (!processAlive) {
            ready_ = false; failedSince_ = 0; failures_ = 0;
            return {};
        }
        if (probe == ProbeResult::Ready || (newSuccess && now >= lastRequestSuccess &&
            now - lastRequestSuccess < 6000)) {
            ready_ = true; failedSince_ = 0; failures_ = 0;
            return {true, false};
        }
        if (!failedSince_) failedSince_ = now;
        if (probe == ProbeResult::Failed && failures_ < 3) ++failures_;
        const auto elapsed = now >= failedSince_ ? now - failedSince_ : 0;
        const bool restart = elapsed >= 15000;
        if (restart || (failures_ >= 3 && elapsed >= 6000)) ready_ = false;
        return {ready_, restart};
    }
private:
    bool ready_ = false;
    unsigned failures_ = 0;
    std::uint64_t failedSince_ = 0, lastSuccess_ = 0;
};

// The bundled Umi HTTP server is single-threaded. Keep probes and warmup
// out of its request queue while score recognition is active or waiting.
class RequestCoordinator {
public:
    std::unique_lock<std::timed_mutex> AcquireForeground() {
        ++foregroundWaiting_;
        std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
        (void)lock.try_lock_for(std::chrono::milliseconds(3000));
        --foregroundWaiting_;
        return lock;
    }
    std::unique_lock<std::timed_mutex> TryBackground() {
        std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
        if (!foregroundWaiting_.load()) (void)lock.try_lock();
        return lock;
    }
    void RecordSuccess(std::uint64_t now) {
        auto old = lastSuccess_.load();
        while (now > old && !lastSuccess_.compare_exchange_weak(old, now)) {}
    }
    std::uint64_t LastSuccessAt() const { return lastSuccess_.load(); }
private:
    std::timed_mutex mutex_;
    std::atomic<unsigned> foregroundWaiting_{0};
    std::atomic<std::uint64_t> lastSuccess_{0};
};
// Keep one coordinator for all OCR callers throughout process shutdown.
inline RequestCoordinator& ServiceRequests = *new RequestCoordinator;
}
