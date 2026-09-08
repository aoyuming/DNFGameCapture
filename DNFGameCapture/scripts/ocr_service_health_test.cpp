#include "../OcrServiceHealth.h"
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
using namespace dnf::ocr;
int main() {
    try {
        HealthMonitor health;
        CHECK(!health.Observe(1000, true, ProbeResult::Failed, 0).ready);
        CHECK(health.Observe(2000, true, ProbeResult::Ready, 0).ready);
        CHECK(health.Observe(3000, true, ProbeResult::Failed, 0).ready);
        CHECK(health.Observe(5000, true, ProbeResult::Ready, 0).ready);
        for (std::uint64_t now = 6000; now < 66000; now += 1000)
            CHECK(health.Observe(now, true, ProbeResult::Busy, now - 50).ready);
        CHECK(!health.Observe(66000, false, ProbeResult::Busy, 65950).ready);
        CHECK(health.Observe(67000, true, ProbeResult::Ready, 0).ready);
        CHECK(health.Observe(68000, true, ProbeResult::Failed, 0).ready);
        CHECK(health.Observe(73999, true, ProbeResult::Failed, 0).ready);
        CHECK(!health.Observe(74000, true, ProbeResult::Failed, 0).ready);
        CHECK(!health.Observe(82999, true, ProbeResult::Failed, 0).restart);
        CHECK(health.Observe(83000, true, ProbeResult::Failed, 0).restart);
        health.BeginProcessStart(83500);
        CHECK(!health.Observe(89500, true, ProbeResult::Failed, 0).restart);
        CHECK(!health.Observe(98499, true, ProbeResult::Failed, 0).restart);
        CHECK(health.Observe(98500, true, ProbeResult::Failed, 0).restart);
        CHECK(health.Observe(99000, true, ProbeResult::Ready, 0).ready);
        CHECK(!health.Observe(99001, true, ProbeResult::Ready, 0).restart);
        HealthMonitor stuck;
        CHECK(stuck.Observe(1000, true, ProbeResult::Ready, 0).ready);
        CHECK(stuck.Observe(2000, true, ProbeResult::Busy, 0).ready);
        CHECK(!stuck.Observe(17000, true, ProbeResult::Busy, 0).ready);
        CHECK(stuck.Observe(17000, true, ProbeResult::Busy, 0).restart);
        RequestCoordinator requests;
        {
            auto foreground = requests.AcquireForeground();
            auto result = std::async(std::launch::async, [&] {
                auto background = requests.TryBackground();
                return background.owns_lock();
            });
            CHECK(!result.get());
        }
        CHECK(requests.TryBackground().owns_lock());
        requests.RecordSuccess(100);
        requests.RecordSuccess(50);
        CHECK(requests.LastSuccessAt() == 100);
        std::atomic<int> active{0};
        std::vector<std::future<void>> calls;
        for (int thread = 0; thread < 4; ++thread) calls.push_back(std::async(std::launch::async, [&] {
            for (int i = 0; i < 50; ++i) {
                auto foreground = requests.AcquireForeground();
                CHECK(foreground.owns_lock());
                CHECK(active.fetch_add(1) == 0);
                std::this_thread::yield();
                CHECK(active.fetch_sub(1) == 1);
            }
        }));
        for (auto& call : calls) call.get();
        std::cout << "OCR health: transient load, sustained failure, process exit, recovery and request priority passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
