#pragma once

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif

namespace luv {

struct AuditEvent {
    uint64_t sequence = 0;
    uint64_t timestamp_ns = 0;
    uint64_t order_id = 0;
    int64_t price = 0;
    int64_t quantity = 0;
    uint16_t trader_id = 0;
    uint8_t side = 0;
    uint8_t type = 0;
    uint8_t previous_hash[32]{};
    uint8_t hash[32]{};
};
static_assert(sizeof(AuditEvent) == 112, "AuditEvent wire layout changed");

class DurableAuditLog {
public:
    DurableAuditLog() = default;
    ~DurableAuditLog() { close(); }
    DurableAuditLog(const DurableAuditLog&) = delete;
    DurableAuditLog& operator=(const DurableAuditLog&) = delete;

    [[nodiscard]] bool open(const char* path, uint32_t sync_interval = 100) noexcept {
        if (!path || _fd >= 0 || sync_interval == 0) return false;
        _fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (_fd < 0) return false;
        _sync_interval = sync_interval;
        _pending = 0;
        _tail.fill(0);

        struct stat metadata{};
        if (::fstat(_fd, &metadata) != 0 ||
            metadata.st_size % static_cast<off_t>(sizeof(AuditEvent)) != 0) {
            close();
            return false;
        }
        _sequence = 0;
        _tail.fill(0);
        if (!validate_existing_records(metadata.st_size)) {
            close();
            return false;
        }
        return true;
    }

    [[nodiscard]] bool append(uint64_t timestamp_ns, uint64_t order_id,
                              int64_t price, int64_t quantity,
                              uint16_t trader_id, uint8_t side,
                              uint8_t type) noexcept {
        if (_fd < 0 || order_id == 0 || quantity <= 0 || side > 1) return false;

        AuditEvent event{};
        event.sequence = _sequence;
        event.timestamp_ns = timestamp_ns;
        event.order_id = order_id;
        event.price = price;
        event.quantity = quantity;
        event.trader_id = trader_id;
        event.side = side;
        event.type = type;
        std::memcpy(event.previous_hash, _tail.data(), _tail.size());

#if defined(__APPLE__)
        CC_SHA256_CTX ctx;
        CC_SHA256_Init(&ctx);
        CC_SHA256_Update(&ctx, event.previous_hash, sizeof(event.previous_hash));
        CC_SHA256_Update(&ctx, &event.sequence,
                         sizeof(event) - sizeof(event.hash) - sizeof(event.previous_hash));
        CC_SHA256_Final(event.hash, &ctx);
#else
        SHA256_CTX ctx;
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, event.previous_hash, sizeof(event.previous_hash));
        SHA256_Update(&ctx, &event.sequence,
                      sizeof(event) - sizeof(event.hash) - sizeof(event.previous_hash));
        SHA256_Final(event.hash, &ctx);
#endif

        if (!write_all(&event, sizeof(event))) return false;
        std::memcpy(_tail.data(), event.hash, _tail.size());
        ++_sequence;
        if (++_pending < _sync_interval) return true;
        return flush();
    }

    [[nodiscard]] bool flush() noexcept {
        if (_fd < 0) return false;
        if (::fsync(_fd) != 0) return false;
        _pending = 0;
        return true;
    }

    void close() noexcept {
        if (_fd < 0) return;
        (void)flush();
        (void)::close(_fd);
        _fd = -1;
    }

    [[nodiscard]] bool is_open() const noexcept { return _fd >= 0; }
    [[nodiscard]] uint64_t size() const noexcept { return _sequence; }

private:
    [[nodiscard]] bool validate_existing_records(off_t bytes) noexcept {
        if (::lseek(_fd, 0, SEEK_SET) < 0) return false;
        AuditEvent event{};
        std::array<uint8_t, 32> previous{};
        while (bytes > 0) {
            if (::read(_fd, &event, sizeof(event)) !=
                static_cast<ssize_t>(sizeof(event))) return false;
            if (event.sequence != _sequence ||
                std::memcmp(event.previous_hash, previous.data(), previous.size()) != 0 ||
                !valid_event(event)) return false;
            std::memcpy(previous.data(), event.hash, previous.size());
            ++_sequence;
            bytes -= static_cast<off_t>(sizeof(event));
        }
        _tail = previous;
        return ::lseek(_fd, 0, SEEK_END) >= 0;
    }

    [[nodiscard]] static bool valid_event(const AuditEvent& event) noexcept {
#if defined(__APPLE__)
        std::array<uint8_t, 32> expected{};
        CC_SHA256_CTX ctx;
        CC_SHA256_Init(&ctx);
        CC_SHA256_Update(&ctx, event.previous_hash, sizeof(event.previous_hash));
        CC_SHA256_Update(&ctx, &event.sequence,
                         sizeof(event) - sizeof(event.hash) - sizeof(event.previous_hash));
        CC_SHA256_Final(expected.data(), &ctx);
        return std::memcmp(expected.data(), event.hash, expected.size()) == 0;
#else
        std::array<uint8_t, 32> expected{};
        SHA256_CTX ctx;
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, event.previous_hash, sizeof(event.previous_hash));
        SHA256_Update(&ctx, &event.sequence,
                      sizeof(event) - sizeof(event.hash) - sizeof(event.previous_hash));
        SHA256_Final(expected.data(), &ctx);
        return std::memcmp(expected.data(), event.hash, expected.size()) == 0;
#endif
    }

    [[nodiscard]] bool write_all(const void* data, size_t bytes) noexcept {
        const auto* cursor = static_cast<const uint8_t*>(data);
        while (bytes != 0) {
            const ssize_t written = ::write(_fd, cursor, bytes);
            if (written < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            if (written == 0) return false;
            cursor += written;
            bytes -= static_cast<size_t>(written);
        }
        return true;
    }

    int _fd = -1;
    uint32_t _sync_interval = 100;
    uint32_t _pending = 0;
    uint64_t _sequence = 0;
    std::array<uint8_t, 32> _tail{};
};

class ShutdownController {
public:
    ShutdownController() noexcept { install(); }
    ShutdownController(const ShutdownController&) = delete;
    ShutdownController& operator=(const ShutdownController&) = delete;

    static void request_shutdown() noexcept {
        _requested.store(true, std::memory_order_release);
    }

    [[nodiscard]] bool requested() const noexcept {
        return _requested.load(std::memory_order_acquire);
    }

    void clear() noexcept { _requested.store(false, std::memory_order_release); }

private:
    static void handle_signal(int) noexcept {
        request_shutdown();
    }

    void install() noexcept {
        struct sigaction action{};
        action.sa_handler = &ShutdownController::handle_signal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        (void)::sigaction(SIGINT, &action, nullptr);
        (void)::sigaction(SIGTERM, &action, nullptr);
    }

    inline static std::atomic<bool> _requested{false};
};

class OrderRateLimiter {
public:
    explicit OrderRateLimiter(uint32_t max_pending = 10'000) noexcept
        : _max_pending(max_pending) {}

    [[nodiscard]] bool try_acquire() noexcept {
        uint32_t current = _pending.load(std::memory_order_relaxed);
        while (current < _max_pending &&
               !_pending.compare_exchange_weak(
                   current, current + 1, std::memory_order_acq_rel,
                   std::memory_order_relaxed)) {}
        return current < _max_pending;
    }

    void release() noexcept {
        uint32_t current = _pending.load(std::memory_order_relaxed);
        while (current != 0 &&
               !_pending.compare_exchange_weak(
                   current, current - 1, std::memory_order_acq_rel,
                   std::memory_order_relaxed)) {}
    }

    [[nodiscard]] uint32_t pending() const noexcept {
        return _pending.load(std::memory_order_acquire);
    }

private:
    const uint32_t _max_pending;
    std::atomic<uint32_t> _pending{0};
};

enum class SequenceResult : uint8_t {
    kFirst = 0,
    kNext,
    kDuplicate,
    kGap,
    kOutOfOrder,
};

enum class SequenceGapClass : uint8_t {
    kNone = 0,
    kRecoverable,
    kFatal,
};

class SequenceTracker {
public:
    static constexpr uint64_t kMaxRecoverableGap = 100;

    [[nodiscard]] SequenceResult observe(uint64_t sequence) noexcept {
        if (!_initialized) {
            _initialized = true;
            _next = sequence + 1;
            _last = sequence;
            return SequenceResult::kFirst;
        }
        if (sequence == _next) {
            ++_next;
            _last = sequence;
            return SequenceResult::kNext;
        }
        if (sequence < _next)
            return sequence + 1 == _next
                ? SequenceResult::kDuplicate
                : SequenceResult::kOutOfOrder;
        const uint64_t gap = sequence - _next;
        _gaps += gap;
        _last = sequence;
        _gap_size = gap;
        _gap_class = gap <= kMaxRecoverableGap
                    ? SequenceGapClass::kRecoverable
                    : SequenceGapClass::kFatal;
        _gap_pending = _gap_class == SequenceGapClass::kRecoverable;
        _invalid = _gap_class == SequenceGapClass::kFatal;
        _next = sequence + 1;
        return SequenceResult::kGap;
    }

    [[nodiscard]] bool gap_pending() const noexcept { return _gap_pending; }
    [[nodiscard]] bool invalid() const noexcept { return _invalid; }
    [[nodiscard]] SequenceGapClass gap_class() const noexcept {
        return _gap_class;
    }
    [[nodiscard]] uint64_t last_gap_size() const noexcept { return _gap_size; }

    [[nodiscard]] bool acknowledge_retransmit(
        uint64_t first_missing, uint64_t last_missing) noexcept {
        if (!_gap_pending || first_missing > last_missing ||
            last_missing - first_missing + 1 != _gap_size) return false;
        _gap_pending = false;
        _gap_class = SequenceGapClass::kNone;
        _gap_size = 0;
        return true;
    }

    void reset() noexcept {
        _initialized = false;
        _next = 0;
        _last = 0;
        _gaps = 0;
        _gap_size = 0;
        _gap_class = SequenceGapClass::kNone;
        _gap_pending = false;
        _invalid = false;
    }

    [[nodiscard]] uint64_t next() const noexcept { return _next; }
    [[nodiscard]] uint64_t gaps() const noexcept { return _gaps; }
    [[nodiscard]] bool initialized() const noexcept { return _initialized; }

private:
    bool _initialized = false;
    uint64_t _next = 0;
    uint64_t _last = 0;
    uint64_t _gaps = 0;
    uint64_t _gap_size = 0;
    SequenceGapClass _gap_class = SequenceGapClass::kNone;
    bool _gap_pending = false;
    bool _invalid = false;
};

class CircuitBreaker {
public:
    explicit CircuitBreaker(uint32_t max_failures = 1) noexcept
        : _max_failures(max_failures == 0 ? 1 : max_failures) {}

    void record_failure() noexcept {
        uint32_t failures = _failures.load(std::memory_order_relaxed);
        while (failures < _max_failures &&
               !_failures.compare_exchange_weak(
                   failures, failures + 1, std::memory_order_acq_rel,
                   std::memory_order_relaxed)) {}
        if (failures + 1 >= _max_failures)
            _tripped.store(true, std::memory_order_release);
    }

    void record_success() noexcept {
        _failures.store(0, std::memory_order_release);
    }

    void trip() noexcept { _tripped.store(true, std::memory_order_release); }
    void reset() noexcept {
        _failures.store(0, std::memory_order_release);
        _tripped.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool allow() const noexcept {
        return !_tripped.load(std::memory_order_acquire);
    }
    [[nodiscard]] uint32_t failures() const noexcept {
        return _failures.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool tripped() const noexcept {
        return _tripped.load(std::memory_order_acquire);
    }

private:
    const uint32_t _max_failures;
    std::atomic<uint32_t> _failures{0};
    std::atomic<bool> _tripped{false};
};

struct ExecutionReport {
    uint64_t order_id = 0;
    int64_t filled_quantity = 0;
    bool terminal = false;
};

template <uint32_t Capacity = 1u << 16>
class ReconciliationLedger {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Reconciliation capacity must be a power of two");

public:
    [[nodiscard]] bool register_order(uint64_t order_id,
                                      int64_t quantity) noexcept {
        if (order_id == 0 || quantity <= 0) return false;
        Entry* entry = find_or_empty(order_id);
        if (!entry || entry->order_id != 0) return false;
        entry->order_id = order_id;
        entry->remaining = quantity;
        ++_open;
        return true;
    }

    [[nodiscard]] bool apply(const ExecutionReport& report) noexcept {
        if (report.order_id == 0 || report.filled_quantity <= 0) return false;
        Entry* entry = find(report.order_id);
        if (!entry || report.filled_quantity > entry->remaining) {
            ++_mismatches;
            return false;
        }
        entry->remaining -= report.filled_quantity;
        if (report.terminal || entry->remaining == 0) {
            entry->order_id = 0;
            entry->remaining = 0;
            --_open;
        }
        return true;
    }

    [[nodiscard]] bool cancel(uint64_t order_id) noexcept {
        Entry* entry = find(order_id);
        if (!entry) return false;
        entry->order_id = 0;
        entry->remaining = 0;
        --_open;
        reinsert_cluster(entry);
        return true;
    }

    [[nodiscard]] uint32_t open_orders() const noexcept { return _open; }
    [[nodiscard]] uint64_t mismatches() const noexcept { return _mismatches; }

private:
    struct Entry {
        uint64_t order_id = 0;
        int64_t remaining = 0;
    };

    [[nodiscard]] static uint32_t hash(uint64_t key) noexcept {
        key ^= key >> 30;
        key *= 0xBF58476D1CE4E5B9ULL;
        key ^= key >> 27;
        key *= 0x94D049BB133111EBULL;
        key ^= key >> 31;
        return static_cast<uint32_t>(key) & (Capacity - 1);
    }

    [[nodiscard]] Entry* find(uint64_t order_id) noexcept {
        uint32_t slot = hash(order_id);
        for (uint32_t probe = 0; probe < Capacity; ++probe) {
            if (_entries[slot].order_id == order_id) return &_entries[slot];
            if (_entries[slot].order_id == 0) return nullptr;
            slot = (slot + 1) & (Capacity - 1);
        }
        return nullptr;
    }

    [[nodiscard]] Entry* find_or_empty(uint64_t order_id) noexcept {
        uint32_t slot = hash(order_id);
        for (uint32_t probe = 0; probe < Capacity; ++probe) {
            if (_entries[slot].order_id == order_id ||
                _entries[slot].order_id == 0)
                return &_entries[slot];
            slot = (slot + 1) & (Capacity - 1);
        }
        return nullptr;
    }

    void reinsert_cluster(Entry* removed) noexcept {
        const uintptr_t base = reinterpret_cast<uintptr_t>(_entries.data());
        uint32_t slot = static_cast<uint32_t>(
            (reinterpret_cast<uintptr_t>(removed) - base) / sizeof(Entry));
        slot = (slot + 1) & (Capacity - 1);
        while (_entries[slot].order_id != 0) {
            const Entry displaced = _entries[slot];
            _entries[slot] = Entry{};
            Entry* destination = find_or_empty(displaced.order_id);
            if (!destination) return;
            *destination = displaced;
            slot = (slot + 1) & (Capacity - 1);
        }
    }

    std::array<Entry, Capacity> _entries{};
    uint32_t _open = 0;
    uint64_t _mismatches = 0;
};

class StructuredEventLogger {
public:
    static void order_event(const char* event, uint64_t timestamp_ns,
                            uint64_t order_id, int64_t price,
                            int64_t quantity, uint16_t trader_id,
                            uint8_t side) noexcept {
        if (!event || order_id == 0 || quantity <= 0 || side > 1) return;
        std::fprintf(stderr,
                     "{\"event\":\"%s\",\"timestamp_ns\":%llu,"
                     "\"order_id\":%llu,\"price\":%lld,\"quantity\":%lld,"
                     "\"trader_id\":%u,\"side\":%u}\n",
                     event,
                     static_cast<unsigned long long>(timestamp_ns),
                     static_cast<unsigned long long>(order_id),
                     static_cast<long long>(price),
                     static_cast<long long>(quantity),
                     static_cast<unsigned>(trader_id),
                     static_cast<unsigned>(side));
    }
};

}  // namespace luv
