#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "luv_arena.hpp"
#include "luv_lob.hpp"

int main() {
    luv::Arena arena;
    assert(arena.init());
    luv::LOBEngine lob;
    assert(lob.init(arena));

    luv::TickMsg tick{};
    tick.msg_type = 'A';
    tick.symbol_idx = 0;
    tick.flags = luv::tick_flags::kBuy;
    tick.qty = 1;
    tick.price = 1'000'000;

    constexpr uint32_t iterations = 20'000;
    std::vector<uint64_t> samples;
    samples.reserve(iterations);
    for (uint32_t index = 0; index < iterations; ++index) {
        tick.order_ref = static_cast<uint64_t>(index) + 1;
        const auto start = std::chrono::steady_clock::now();
        lob.process(tick);
        const auto end = std::chrono::steady_clock::now();
        samples.push_back(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
        luv::TickMsg remove{};
        remove.msg_type = 'D';
        remove.symbol_idx = 0;
        remove.order_ref = tick.order_ref;
        lob.process(remove);
    }

    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](uint32_t rank) {
        const size_t index = (samples.size() * rank + 999) / 1000;
        return samples[index == 0 ? 0 : std::min(index - 1, samples.size() - 1)];
    };
    std::printf("local LOB add latency ns: p50=%llu p99=%llu p999=%llu samples=%u\n",
                static_cast<unsigned long long>(percentile(500)),
                static_cast<unsigned long long>(percentile(990)),
                static_cast<unsigned long long>(percentile(999)),
                iterations);
    return 0;
}
