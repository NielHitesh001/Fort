#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <vector>
#include <unistd.h>

#include "luv_recovery.hpp"

namespace {

uint32_t record_checksum(const luv::RecoveryRecord& record) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
    uint32_t hash = 2166136261u;
    for (size_t index = 0; index < offsetof(luv::RecoveryRecord, checksum); ++index) {
        hash ^= bytes[index];
        hash *= 16777619u;
    }
    return hash;
}

void write_large_ledger(const char* path, uint32_t events) {
    const int fd = ::open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    assert(fd >= 0);
    for (uint32_t index = 0; index < events; ++index) {
        luv::RecoveryRecord record{};
        record.type = static_cast<uint8_t>(index == 0
            ? luv::RecoveryEventType::kAdd
            : luv::RecoveryEventType::kFill);
        record.sequence = index;
        record.order_id = 99;
        record.quantity = index == 0 ? events : 1;
        record.price = 1'000'000;
        record.timestamp_ns = index;
        record.checksum = record_checksum(record);
        assert(::write(fd, &record, sizeof(record)) ==
               static_cast<ssize_t>(sizeof(record)));
    }
    assert(::close(fd) == 0);
}

void test_replay_100k_messages() {
    const char* path = "/tmp/luv-recovery-100k.bin";
    constexpr uint32_t events = 100'000;
    write_large_ledger(path, events);

    luv::RecoveryLedger ledger;
    assert(ledger.open(path));
    std::vector<luv::RecoveredOrder> orders(2);
    uint32_t count = 0;
    const auto start = std::chrono::steady_clock::now();
    assert(ledger.replay(orders.data(), static_cast<uint32_t>(orders.size()), count));
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    assert(count == 1);
    assert(orders[0].remaining == 1);
    assert(elapsed < 50'000);
    std::printf("replay 100k: %lld us\n", static_cast<long long>(elapsed));
    ledger.close();
    ::unlink(path);
}

void test_replay_determinism() {
    const char* path = "/tmp/luv-recovery-determinism.bin";
    write_large_ledger(path, 10'000);
    luv::RecoveryLedger ledger;
    assert(ledger.open(path));
    luv::RecoveredOrder first[2]{}, second[2]{};
    uint32_t first_count = 0, second_count = 0;
    assert(ledger.replay(first, 2, first_count));
    assert(ledger.replay(second, 2, second_count));
    assert(first_count == second_count);
    assert(std::memcmp(first, second, sizeof(first)) == 0);
    ledger.close();
    ::unlink(path);
}

void test_recovery_partial_corruption() {
    const char* path = "/tmp/luv-recovery-partial.bin";
    write_large_ledger(path, 128);
    struct stat metadata{};
    assert(::stat(path, &metadata) == 0);
    const off_t sizes[] = {1, metadata.st_size / 2 + 1, metadata.st_size - 1};
    for (off_t size : sizes) {
        const int fd = ::open(path, O_WRONLY);
        assert(fd >= 0);
        assert(::ftruncate(fd, size) == 0);
        ::close(fd);
        luv::RecoveryLedger truncated;
        assert(!truncated.open(path));
        write_large_ledger(path, 128);
    }
    const int fd = ::open(path, O_RDWR);
    assert(fd >= 0);
    uint8_t corrupted = 0xFF;
    assert(::pwrite(fd, &corrupted, 1, sizeof(luv::RecoveryRecord) + 8) == 1);
    ::close(fd);
    luv::RecoveryLedger corrupted_ledger;
    assert(corrupted_ledger.open(path));
    luv::RecoveredOrder orders[2]{};
    uint32_t count = 0;
    assert(!corrupted_ledger.replay(orders, 2, count));
    corrupted_ledger.close();
    ::unlink(path);
}

void test_recovery_latency_slo() {
    const char* path = "/tmp/luv-recovery-latency.bin";
    write_large_ledger(path, 100'000);
    std::vector<long long> samples;
    samples.reserve(5);
    for (int iteration = 0; iteration < 5; ++iteration) {
        luv::RecoveryLedger ledger;
        assert(ledger.open(path));
        luv::RecoveredOrder orders[2]{};
        uint32_t count = 0;
        const auto start = std::chrono::steady_clock::now();
        assert(ledger.replay(orders, 2, count));
        samples.push_back(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count());
    }
    std::sort(samples.begin(), samples.end());
    assert(samples[4] < 50'000);
    std::printf("replay latency p50=%lld us p99=%lld us\n", samples[2], samples[4]);
    ::unlink(path);
}

}  // namespace

int main() {
    const char* path = "/tmp/luv-recovery-test.bin";
    ::unlink(path);

    {
        luv::RecoveryLedger ledger;
        assert(ledger.open(path));
        assert(ledger.append_with_side(luv::RecoveryEventType::kAdd, 10, 100,
                           1'000'000, 1, 111));
        assert(ledger.append(luv::RecoveryEventType::kFill, 10, 40));
        assert(ledger.append(luv::RecoveryEventType::kAdd, 11, 50, 1'010'000));
        assert(ledger.append(luv::RecoveryEventType::kCancel, 11, 50));
        assert(ledger.size() == 4);
    }

    {
        luv::RecoveryLedger ledger;
        assert(ledger.open(path));
        luv::RecoveredOrder orders[4]{};
        uint32_t count = 0;
        assert(ledger.replay(orders, 4, count));
        assert(count == 1);
        assert(orders[0].order_id == 10);
        assert(orders[0].remaining == 60);
        assert(orders[0].price == 1'000'000);
        assert(orders[0].side == 1);
        assert(orders[0].timestamp_ns == 111);
    }

    int fd = ::open(path, O_WRONLY | O_APPEND);
    assert(fd >= 0);
    const uint8_t corrupt = 0xFF;
    assert(::write(fd, &corrupt, sizeof(corrupt)) == sizeof(corrupt));
    ::close(fd);

    luv::RecoveryLedger corrupted;
    assert(!corrupted.open(path));
    corrupted.close();
    ::unlink(path);

    test_replay_100k_messages();
    test_replay_determinism();
    test_recovery_partial_corruption();
    test_recovery_latency_slo();

    std::printf("crash recovery replay passed: determinism, latency, and corruption checks\n");
    return 0;
}
