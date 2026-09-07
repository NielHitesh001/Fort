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

struct AuditManifestCheckpoint {
    uint32_t magic = 0x4D4E4653; // "SFNM"
    uint32_t version = 1;
    uint64_t start_sequence = 0;
    uint64_t end_sequence = 0;
    uint64_t start_timestamp_ns = 0;
    uint64_t end_timestamp_ns = 0;
    uint64_t event_count = 0;
    uint8_t start_hash[32]{};
    uint8_t end_hash[32]{};
    uint8_t manifest_hash[32]{};
};
static_assert(sizeof(AuditManifestCheckpoint) == 144, "Manifest record layout changed");

class DurableAuditLog {
public:
    DurableAuditLog() = default;
    ~DurableAuditLog() { close(); }
    DurableAuditLog(const DurableAuditLog&) = delete;
    DurableAuditLog& operator=(const DurableAuditLog&) = delete;

    [[nodiscard]] bool open(const char* path, uint32_t sync_interval = 100) noexcept {
        if (!path || _fd >= 0 || sync_interval == 0) return false;
        _fd = ::open(path, O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (_fd < 0) return false;
        _sync_interval = sync_interval;
        _pending = 0;
        _tail.fill(0);

        struct flock fl{};
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        fl.l_start = 0;
        fl.l_len = 0;
        (void)::fcntl(_fd, F_SETLK, &fl);

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

    [[nodiscard]] bool read_range(uint64_t start_seq, uint64_t end_seq,
                                  AuditEvent* out_events, size_t max_events,
                                  size_t& count) const noexcept {
        count = 0;
        if (_fd < 0 || !out_events || max_events == 0 || start_seq > end_seq) return false;
        const off_t start_offset = static_cast<off_t>(start_seq * sizeof(AuditEvent));
        const uint64_t req_count = end_seq - start_seq + 1;
        const size_t to_read = static_cast<size_t>(req_count < max_events ? req_count : max_events);
        for (size_t i = 0; i < to_read; ++i) {
            const off_t offset = start_offset + static_cast<off_t>(i * sizeof(AuditEvent));
            if (::pread(_fd, &out_events[i], sizeof(AuditEvent), offset) != static_cast<ssize_t>(sizeof(AuditEvent))) {
                break;
            }
            ++count;
        }
        return count > 0;
    }

    [[nodiscard]] static bool verify_log_integrity(const char* log_path,
                                                   uint64_t& valid_events,
                                                   uint8_t* out_root_hash = nullptr) noexcept {
        valid_events = 0;
        if (!log_path) return false;
        int fd = ::open(log_path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return false;
        struct stat st{};
        if (::fstat(fd, &st) != 0 || st.st_size % static_cast<off_t>(sizeof(AuditEvent)) != 0) {
            ::close(fd);
            return false;
        }
        const uint64_t total = static_cast<uint64_t>(st.st_size / static_cast<off_t>(sizeof(AuditEvent)));
        AuditEvent ev{};
        std::array<uint8_t, 32> prev_hash{};
        for (uint64_t seq = 0; seq < total; ++seq) {
            if (::read(fd, &ev, sizeof(ev)) != sizeof(ev)) {
                ::close(fd);
                return false;
            }
            if (ev.sequence != seq ||
                std::memcmp(ev.previous_hash, prev_hash.data(), 32) != 0 ||
                !valid_event(ev)) {
                ::close(fd);
                return false;
            }
            std::memcpy(prev_hash.data(), ev.hash, 32);
            ++valid_events;
        }
        ::close(fd);
        if (out_root_hash) {
            std::memcpy(out_root_hash, prev_hash.data(), 32);
        }
        return true;
    }

    [[nodiscard]] static bool export_json(const char* log_path,
                                          const char* out_json_path,
                                          uint64_t start_seq = 0,
                                          uint64_t end_seq = UINT64_MAX) noexcept {
        if (!log_path || !out_json_path) return false;
        int lfd = ::open(log_path, O_RDONLY | O_CLOEXEC);
        if (lfd < 0) return false;
        FILE* out = ::fopen(out_json_path, "w");
        if (!out) { ::close(lfd); return false; }

        struct stat st{};
        if (::fstat(lfd, &st) != 0) { ::close(lfd); ::fclose(out); return false; }
        const uint64_t total = static_cast<uint64_t>(st.st_size / static_cast<off_t>(sizeof(AuditEvent)));
        if (start_seq >= total) { ::close(lfd); ::fclose(out); return false; }
        const uint64_t limit_seq = end_seq < total ? end_seq : (total - 1);

        if (::lseek(lfd, static_cast<off_t>(start_seq * sizeof(AuditEvent)), SEEK_SET) < 0) {
            ::close(lfd); ::fclose(out); return false;
        }

        std::fprintf(out, "[\n");
        AuditEvent ev{};
        for (uint64_t seq = start_seq; seq <= limit_seq; ++seq) {
            if (::read(lfd, &ev, sizeof(ev)) != sizeof(ev)) break;
            std::fprintf(out, "  {\"seq\":%llu,\"timestamp_ns\":%llu,\"order_id\":%llu,\"price\":%lld,\"qty\":%lld,\"trader\":%u,\"side\":%u,\"type\":%u}%s\n",
                         static_cast<unsigned long long>(ev.sequence),
                         static_cast<unsigned long long>(ev.timestamp_ns),
                         static_cast<unsigned long long>(ev.order_id),
                         static_cast<long long>(ev.price),
                         static_cast<long long>(ev.quantity),
                         static_cast<unsigned>(ev.trader_id),
                         static_cast<unsigned>(ev.side),
                         static_cast<unsigned>(ev.type),
                         (seq == limit_seq) ? "" : ",");
        }
        std::fprintf(out, "]\n");
        ::close(lfd);
        ::fclose(out);
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

    [[nodiscard]] bool write_manifest_checkpoint(const char* manifest_path,
                                                uint64_t start_seq,
                                                uint64_t end_seq) noexcept {
        if (!manifest_path || _fd < 0 || start_seq > end_seq || end_seq >= _sequence)
            return false;

        (void)flush();
        const off_t start_offset = static_cast<off_t>(start_seq * sizeof(AuditEvent));
        const uint64_t count = end_seq - start_seq + 1;
        AuditEvent first_event{}, last_event{};
        if (::pread(_fd, &first_event, sizeof(first_event), start_offset) != static_cast<ssize_t>(sizeof(first_event)))
            return false;
        const off_t end_offset = static_cast<off_t>(end_seq * sizeof(AuditEvent));
        if (::pread(_fd, &last_event, sizeof(last_event), end_offset) != static_cast<ssize_t>(sizeof(last_event)))
            return false;

        AuditManifestCheckpoint checkpoint{};
        checkpoint.start_sequence = start_seq;
        checkpoint.end_sequence = end_seq;
        checkpoint.start_timestamp_ns = first_event.timestamp_ns;
        checkpoint.end_timestamp_ns = last_event.timestamp_ns;
        checkpoint.event_count = count;
        std::memcpy(checkpoint.start_hash, first_event.previous_hash, 32);
        std::memcpy(checkpoint.end_hash, last_event.hash, 32);

#if defined(__APPLE__)
        CC_SHA256_CTX ctx;
        CC_SHA256_Init(&ctx);
        CC_SHA256_Update(&ctx, &checkpoint, offsetof(AuditManifestCheckpoint, manifest_hash));
        CC_SHA256_Final(checkpoint.manifest_hash, &ctx);
#else
        SHA256_CTX ctx;
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, &checkpoint, offsetof(AuditManifestCheckpoint, manifest_hash));
        SHA256_Final(checkpoint.manifest_hash, &ctx);
#endif

        int mfd = ::open(manifest_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (mfd < 0) return false;
        const ssize_t written = ::write(mfd, &checkpoint, sizeof(checkpoint));
        (void)::fsync(mfd);
        (void)::close(mfd);
        return written == static_cast<ssize_t>(sizeof(checkpoint));
    }

    [[nodiscard]] static bool verify_manifest(const char* log_path,
                                              const char* manifest_path) noexcept {
        if (!log_path || !manifest_path) return false;
        int mfd = ::open(manifest_path, O_RDONLY | O_CLOEXEC);
        if (mfd < 0) return false;

        AuditManifestCheckpoint checkpoint{};
        const ssize_t mread = ::read(mfd, &checkpoint, sizeof(checkpoint));
        ::close(mfd);
        if (mread != static_cast<ssize_t>(sizeof(checkpoint))) return false;

        if (checkpoint.magic != 0x4D4E4653 || checkpoint.version != 1) return false;

        uint8_t expected_mhash[32]{};
#if defined(__APPLE__)
        CC_SHA256_CTX ctx;
        CC_SHA256_Init(&ctx);
        CC_SHA256_Update(&ctx, &checkpoint, offsetof(AuditManifestCheckpoint, manifest_hash));
        CC_SHA256_Final(expected_mhash, &ctx);
#else
        SHA256_CTX ctx;
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, &checkpoint, offsetof(AuditManifestCheckpoint, manifest_hash));
        SHA256_Final(expected_mhash, &ctx);
#endif
        if (std::memcmp(expected_mhash, checkpoint.manifest_hash, 32) != 0) return false;

        int lfd = ::open(log_path, O_RDONLY | O_CLOEXEC);
        if (lfd < 0) return false;
        struct stat st{};
        if (::fstat(lfd, &st) != 0) { ::close(lfd); return false; }
        const uint64_t total_events = static_cast<uint64_t>(st.st_size / static_cast<off_t>(sizeof(AuditEvent)));
        if (checkpoint.end_sequence >= total_events) { ::close(lfd); return false; }

        AuditEvent ev{};
        std::array<uint8_t, 32> prev_hash{};
        if (::lseek(lfd, static_cast<off_t>(checkpoint.start_sequence * sizeof(AuditEvent)), SEEK_SET) < 0) {
            ::close(lfd); return false;
        }
        for (uint64_t seq = checkpoint.start_sequence; seq <= checkpoint.end_sequence; ++seq) {
            if (::read(lfd, &ev, sizeof(ev)) != sizeof(ev)) { ::close(lfd); return false; }
            if (seq == checkpoint.start_sequence) {
                if (std::memcmp(ev.previous_hash, checkpoint.start_hash, 32) != 0) {
                    ::close(lfd); return false;
                }
            } else {
                if (std::memcmp(ev.previous_hash, prev_hash.data(), 32) != 0) {
                    ::close(lfd); return false;
                }
            }
            if (!valid_event(ev)) { ::close(lfd); return false; }
            std::memcpy(prev_hash.data(), ev.hash, 32);
        }
        ::close(lfd);
        return std::memcmp(prev_hash.data(), checkpoint.end_hash, 32) == 0;
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

enum class CircuitBreakerState : uint8_t {
    kClosed = 0,
    kOpen = 1,
    kHalfOpen = 2,
};

class CircuitBreaker {
public:
    using State = CircuitBreakerState;

    explicit CircuitBreaker(uint32_t max_failures = 1, uint64_t cool_down_ns = 0) noexcept
        : _max_failures(max_failures == 0 ? 1 : max_failures),
          _cool_down_ns(cool_down_ns) {}

    void record_failure(uint64_t timestamp_ns = 0) noexcept {
        const State current = _state.load(std::memory_order_acquire);
        if (current == State::kHalfOpen) {
            trip(timestamp_ns);
            return;
        }
        uint32_t failures = _failures.load(std::memory_order_relaxed);
        while (failures < _max_failures &&
               !_failures.compare_exchange_weak(
                   failures, failures + 1, std::memory_order_acq_rel,
                   std::memory_order_relaxed)) {}
        if (failures + 1 >= _max_failures) {
            trip(timestamp_ns);
        }
    }

    void record_rejection(uint64_t timestamp_ns = 0) noexcept {
        const uint32_t max_rejections = _max_consecutive_rejections.load(std::memory_order_relaxed);
        if (max_rejections > 0) {
            const uint32_t rejections = _consecutive_rejections.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (rejections >= max_rejections) {
                trip(timestamp_ns);
                return;
            }
        }
        record_failure(timestamp_ns);
    }

    void record_success() noexcept {
        const State current = _state.load(std::memory_order_acquire);
        if (current == State::kHalfOpen) {
            const uint32_t successes = _half_open_successes.fetch_add(1, std::memory_order_acq_rel) + 1;
            const uint32_t needed = _half_open_successes_needed.load(std::memory_order_relaxed);
            if (successes >= needed) {
                reset();
            }
        } else if (current == State::kClosed) {
            _failures.store(0, std::memory_order_release);
            _consecutive_rejections.store(0, std::memory_order_release);
        }
    }

    void trip(uint64_t timestamp_ns = 0) noexcept {
        _trip_timestamp_ns.store(timestamp_ns, std::memory_order_release);
        _state.store(State::kOpen, std::memory_order_release);
        _tripped.store(true, std::memory_order_release);
    }

    void reset() noexcept {
        _failures.store(0, std::memory_order_release);
        _consecutive_rejections.store(0, std::memory_order_release);
        _half_open_probes_issued.store(0, std::memory_order_release);
        _half_open_successes.store(0, std::memory_order_release);
        _trip_timestamp_ns.store(0, std::memory_order_release);
        _state.store(State::kClosed, std::memory_order_release);
        _tripped.store(false, std::memory_order_release);
    }

    void set_cool_down_ns(uint64_t cool_down_ns) noexcept {
        _cool_down_ns.store(cool_down_ns, std::memory_order_release);
    }

    [[nodiscard]] uint64_t cool_down_ns() const noexcept {
        return _cool_down_ns.load(std::memory_order_acquire);
    }

    void set_half_open_params(uint32_t max_probes, uint32_t successes_needed) noexcept {
        _half_open_enabled.store(true, std::memory_order_release);
        _half_open_max_probes.store(max_probes == 0 ? 1 : max_probes, std::memory_order_release);
        _half_open_successes_needed.store(successes_needed == 0 ? 1 : successes_needed, std::memory_order_release);
    }

    void set_max_consecutive_rejections(uint32_t max_rejections) noexcept {
        _max_consecutive_rejections.store(max_rejections, std::memory_order_release);
    }

    void set_rate_limit(uint32_t max_rate, uint64_t window_ns) noexcept {
        _max_rate_per_window.store(max_rate, std::memory_order_release);
        _rate_window_ns.store(window_ns, std::memory_order_release);
    }

    void set_max_loss_limit(int64_t loss_limit) noexcept {
        _max_loss_limit.store(loss_limit, std::memory_order_release);
    }

    [[nodiscard]] bool check_loss_limit(int64_t current_loss, uint64_t timestamp_ns = 0) noexcept {
        const int64_t limit = _max_loss_limit.load(std::memory_order_relaxed);
        if (limit > 0 && current_loss >= limit) {
            trip(timestamp_ns);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool allow(uint64_t now_ns) noexcept {
        State current = _state.load(std::memory_order_acquire);
        if (current == State::kClosed) {
            const uint32_t max_rate = _max_rate_per_window.load(std::memory_order_relaxed);
            const uint64_t win_ns = _rate_window_ns.load(std::memory_order_relaxed);
            if (max_rate > 0 && win_ns > 0 && now_ns > 0) {
                uint64_t win_start = _window_start_ns.load(std::memory_order_relaxed);
                if (now_ns < win_start || (now_ns - win_start) >= win_ns) {
                    _window_start_ns.store(now_ns, std::memory_order_relaxed);
                    _window_count.store(1, std::memory_order_relaxed);
                } else {
                    const uint32_t count = _window_count.fetch_add(1, std::memory_order_acq_rel) + 1;
                    if (count > max_rate) {
                        trip(now_ns);
                        return false;
                    }
                }
            }
            return true;
        }

        if (current == State::kOpen) {
            const uint64_t cool_down = _cool_down_ns.load(std::memory_order_relaxed);
            if (cool_down > 0 && now_ns > 0) {
                const uint64_t trip_time = _trip_timestamp_ns.load(std::memory_order_relaxed);
                if (now_ns >= trip_time && (now_ns - trip_time) >= cool_down) {
                    if (_half_open_enabled.load(std::memory_order_relaxed)) {
                        State expected = State::kOpen;
                        if (_state.compare_exchange_strong(expected, State::kHalfOpen,
                                                           std::memory_order_acq_rel,
                                                           std::memory_order_acquire)) {
                            _half_open_probes_issued.store(0, std::memory_order_release);
                            _half_open_successes.store(0, std::memory_order_release);
                            current = State::kHalfOpen;
                        }
                    } else {
                        reset();
                        return true;
                    }
                }
            }
        }

        if (current == State::kHalfOpen) {
            const uint32_t max_probes = _half_open_max_probes.load(std::memory_order_relaxed);
            const uint32_t probes = _half_open_probes_issued.fetch_add(1, std::memory_order_acq_rel);
            if (probes < max_probes) {
                return true;
            }
            return false;
        }

        return false;
    }

    [[nodiscard]] bool allow() const noexcept {
        return _state.load(std::memory_order_acquire) == State::kClosed;
    }

    [[nodiscard]] State state() const noexcept {
        return _state.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool is_closed() const noexcept {
        return _state.load(std::memory_order_acquire) == State::kClosed;
    }

    [[nodiscard]] bool is_open() const noexcept {
        return _state.load(std::memory_order_acquire) == State::kOpen;
    }

    [[nodiscard]] bool is_half_open() const noexcept {
        return _state.load(std::memory_order_acquire) == State::kHalfOpen;
    }

    [[nodiscard]] uint32_t failures() const noexcept {
        return _failures.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint32_t consecutive_rejections() const noexcept {
        return _consecutive_rejections.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool tripped() const noexcept {
        return _state.load(std::memory_order_acquire) == State::kOpen;
    }

    [[nodiscard]] uint64_t trip_timestamp_ns() const noexcept {
        return _trip_timestamp_ns.load(std::memory_order_acquire);
    }

private:
    const uint32_t _max_failures;
    std::atomic<uint64_t> _cool_down_ns{0};
    std::atomic<uint64_t> _trip_timestamp_ns{0};
    std::atomic<uint32_t> _failures{0};
    std::atomic<uint32_t> _consecutive_rejections{0};
    std::atomic<uint32_t> _max_consecutive_rejections{0};
    std::atomic<bool> _half_open_enabled{false};
    std::atomic<uint32_t> _half_open_max_probes{1};
    std::atomic<uint32_t> _half_open_successes_needed{1};
    std::atomic<uint32_t> _half_open_probes_issued{0};
    std::atomic<uint32_t> _half_open_successes{0};
    std::atomic<uint32_t> _max_rate_per_window{0};
    std::atomic<uint64_t> _rate_window_ns{0};
    std::atomic<uint64_t> _window_start_ns{0};
    std::atomic<uint32_t> _window_count{0};
    std::atomic<int64_t> _max_loss_limit{0};
    std::atomic<State> _state{State::kClosed};
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

    [[nodiscard]] bool replace(uint64_t order_id,
                               int64_t new_quantity) noexcept {
        if (order_id == 0 || new_quantity <= 0) return false;
        Entry* entry = find(order_id);
        if (!entry) return false;
        entry->remaining = new_quantity;
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

    [[nodiscard]] bool contains(uint64_t order_id) noexcept {
        return order_id != 0 && find(order_id) != nullptr;
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
