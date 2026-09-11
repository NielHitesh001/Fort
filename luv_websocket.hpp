#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <poll.h>
#include <span>
#include <thread>

#include "luv_consumer.hpp"
#include "luv_execution.hpp"

namespace luv::ws {

constexpr uint32_t kMaxConnections = Config::kWebSocketConnections;
// The SPSC ring has to be a power of two.  Make the admission backlog large
// enough for every reservable connection so a burst of valid upgrades is
// limited by the documented pool capacity, rather than an unrelated 64-slot
// staging queue.
constexpr uint32_t kUpgradeQueueCapacity = 1024;
static_assert(kUpgradeQueueCapacity >= kMaxConnections);
// HTTP's first recv can legally contain a complete masked WebSocket frame
// after the upgrade headers. Keep that bounded payload with the queued
// descriptor rather than discarding bytes at the HTTP/WS ownership boundary.
constexpr uint16_t kMaxUpgradePreReadBytes = 2048;

// An upgrade is produced by the HTTP thread and consumed by the WebSocket
// worker. The worker owns the duplicated descriptor after a successful
// enqueue; every rejected path closes that descriptor before returning.
struct Upgrade {
    int fd = -1;
    uint64_t order_id = 0;
    char key[32]{};
    uint16_t pre_read_size = 0;
    std::array<char, kMaxUpgradePreReadBytes> pre_read{};
};

enum class UpgradeResult : uint8_t {
    kAccepted,
    kInvalidKey,
    kInvalidUpgrade,
    kPoolExhausted,
    kQueueFull,
};

class Server {
public:
    struct Diagnostics {
        uint64_t completed_broadcasts = 0;
        uint64_t last_broadcast_latency_ns = 0;
        uint32_t last_fanout_count = 0;
        uint64_t queued_frames = 0;
        uint64_t dropped_frames = 0;
        uint64_t slow_client_closes = 0;
        uint64_t last_backpressure_close_ns = 0;
        uint64_t pings_sent = 0;
        uint64_t pongs_received = 0;
        uint64_t last_pong_rtt_ns = 0;
        uint64_t cleanup_count = 0;
    };

    explicit Server(uint32_t max_connections = kMaxConnections,
                    uint64_t ping_interval_ns = 30'000'000'000ULL,
                    uint64_t pong_timeout_ns = 30'000'000'000ULL) noexcept
        : ping_interval_ns_(ping_interval_ns),
          pong_timeout_ns_(pong_timeout_ns),
          max_connections_(max_connections <= kMaxConnections
                               ? max_connections
                               : kMaxConnections) {}
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // The caller owns this fixed slab for the complete start()/stop()
    // lifetime. Production passes Arena storage; standalone tests supply an
    // explicitly aligned static slab. No WebSocket connection mmap/new is
    // performed by Server.
    [[nodiscard]] bool start(std::span<std::byte> connection_storage) noexcept;
    void stop() noexcept;

    // `pre_read` holds client bytes that arrived in HTTP's initial recv after
    // \r\n\r\n. It is copied into the fixed upgrade queue and parsed by the
    // worker immediately after accepting the socket.
    [[nodiscard]] UpgradeResult enqueue_upgrade(
        int fd, uint64_t order_id, const char* key,
        const char* pre_read = nullptr, size_t pre_read_size = 0) noexcept;
    [[nodiscard]] bool publish(
        const ExecutionGateway::FillEvent& event) noexcept;
    static bool publish_callback(
        void* context, const ExecutionGateway::FillEvent& event) noexcept;

    [[nodiscard]] uint64_t dropped_events() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint32_t active_connections() const noexcept {
        return active_.load(std::memory_order_acquire);
    }

    // These fixed-cost observability values are updated by the worker after a
    // fill has been formatted and appended to every eligible subscriber's
    // outbound buffer. They let a latency test observe the
    // execution-callback-to-queued boundary without a mutex or allocation on
    // the execution path.
    [[nodiscard]] uint64_t completed_broadcasts() const noexcept {
        return completed_broadcasts_.load(std::memory_order_acquire);
    }
    [[nodiscard]] uint64_t last_broadcast_latency_ns() const noexcept {
        return last_broadcast_latency_ns_.load(std::memory_order_acquire);
    }
    [[nodiscard]] uint32_t last_fanout_count() const noexcept {
        return last_fanout_count_.load(std::memory_order_acquire);
    }
    // `sequence` is the zero-based completed-broadcast sequence. Reading a
    // sample only after completed_broadcasts() has advanced past it is safe:
    // the worker publishes the sample before its release-store completion.
    [[nodiscard]] uint64_t broadcast_latency_sample(
        uint64_t sequence) const noexcept;
    [[nodiscard]] Diagnostics diagnostics() const noexcept;

    // Connection slots are entirely pre-allocated. This makes the slot budget
    // testable without exposing its implementation.
    [[nodiscard]] static constexpr size_t connection_storage_bytes() noexcept;
    [[nodiscard]] static constexpr size_t connection_storage_alignment() noexcept;
    [[nodiscard]] static constexpr size_t connection_pool_storage_bytes(
        uint32_t connection_count = kMaxConnections) noexcept;

private:
    static constexpr uint16_t kReadBufferBytes = kMaxUpgradePreReadBytes;
    static constexpr uint16_t kWriteBufferBytes = 4096;
    static constexpr uint64_t kBackpressureCloseNs = 5'000'000ULL;
    // Run bounded connection lifecycle work on a timer, rather than on every
    // fill wake. This preserves fill fanout latency with a full pool while
    // keeping ping/pong and slow-client deadlines within one millisecond.
    static constexpr uint64_t kMaintenanceIntervalNs = 1'000'000ULL;
    static constexpr uint64_t kMaximumMaintenanceDeferralNs =
        1'000'000ULL;
    // A short quiet period coalesces a producer's contiguous fill burst
    // before the worker enters its next O(active) socket-I/O pass.
    static constexpr uint64_t kPostFillQuietPeriodNs = 50'000ULL;
    // Bound control work per worker turn. A fill burst cannot starve socket
    // I/O or upgrade admission indefinitely, while the 128-fill budget still
    // covers the documented 100-order concurrent-fill acceptance case in one
    // dispatch pass.
    static constexpr uint32_t kMaxUpgradesPerTurn = 16;
    static constexpr uint32_t kMaxFillsPerTurn = 128;
    static constexpr uint32_t kSubscriptionBuckets = 2048;
    static constexpr uint32_t kSubscriptionMask = kSubscriptionBuckets - 1U;
    static constexpr uint32_t kLatencySampleCapacity = 1024;
    static constexpr uint32_t kLatencySampleMask =
        kLatencySampleCapacity - 1U;
    static_assert((kSubscriptionBuckets & kSubscriptionMask) == 0,
                  "subscription table must be a power of two");
    static_assert((kLatencySampleCapacity & kLatencySampleMask) == 0,
                  "latency sample table must be a power of two");
    static constexpr uint16_t kNoSubscriptionBucket = UINT16_MAX;

    struct PendingFill {
        ExecutionGateway::FillEvent event{};
        uint64_t published_ns = 0;
    };

    struct Connection {
        int fd = -1;
        uint64_t order_id = 0;
        uint64_t last_ping_ns = 0;
        uint64_t last_pong_ns = 0;
        uint64_t write_blocked_since_ns = 0;
        uint16_t read_size = 0;
        uint16_t write_offset = 0;
        uint16_t write_size = 0;
        uint16_t subscription_bucket = kNoSubscriptionBucket;
        int32_t subscription_prev = -1;
        int32_t subscription_next = -1;
        // Worker-owned intrusive list of freshly queued outbound buffers.
        // Healthy clients are sent directly after a fill batch; only an
        // EAGAIN path arms the kernel write-readiness filter.
        int32_t flush_next = -1;
        bool awaiting_pong = false;
        bool close_after_flush = false;
        bool fragmented_data = false;
        bool flush_queued = false;
        bool write_waiting = false;
        std::array<char, kReadBufferBytes> read_buffer{};
        std::array<char, kWriteBufferBytes> write_buffer{};
    };

    static_assert(sizeof(Connection) <= Config::kWebSocketConnectionSlotBytes,
                  "WebSocket connection storage exceeds the 10 KiB budget");
    struct SubscriptionBucket {
        uint64_t order_id = 0;
        int32_t head = -1;
        // Empty stops a probe. Tombstones retain the probe chain after the
        // last subscriber disconnects and are reused by later upgrades.
        enum class State : uint8_t { kEmpty, kOccupied, kTombstone };
        State state = State::kEmpty;
    };

    enum class FrameResult : uint8_t {
        kIncomplete,
        kConsumed,
        kProtocolError,
        kInvalidUtf8,
        kMessageTooLarge,
    };

    void run() noexcept;
    void accept_upgrade(const Upgrade& upgrade) noexcept;
    void close(Connection& connection) noexcept;
    void close_pending_upgrade(const Upgrade& upgrade) noexcept;
    void reset_subscriptions() noexcept;
    [[nodiscard]] int32_t find_subscription(uint64_t order_id) const noexcept;
    [[nodiscard]] int32_t find_or_create_subscription(
        uint64_t order_id) noexcept;
    [[nodiscard]] bool subscribe(Connection& connection) noexcept;
    void unsubscribe(Connection& connection) noexcept;
    [[nodiscard]] static uint32_t subscription_hash(
        uint64_t order_id) noexcept;

    [[nodiscard]] bool queue_bytes(Connection& connection, const void* bytes,
                                   size_t count) noexcept;
    [[nodiscard]] bool queue_frame(Connection& connection, uint8_t opcode,
                                   const void* payload,
                                   size_t payload_size) noexcept;
    [[nodiscard]] bool queue_close(Connection& connection, uint16_t code,
                                   const void* reason,
                                   size_t reason_size) noexcept;
    [[nodiscard]] bool close_with_code(Connection& connection,
                                       uint16_t code) noexcept;
    [[nodiscard]] bool flush(Connection& connection,
                             uint64_t timestamp_ns) noexcept;
    [[nodiscard]] bool receive(Connection& connection,
                               uint64_t timestamp_ns) noexcept;
    [[nodiscard]] FrameResult consume_frame(Connection& connection,
                                            uint64_t timestamp_ns) noexcept;
    void broadcast(const PendingFill& fill) noexcept;
    void service_keepalive(Connection& connection,
                           uint64_t timestamp_ns) noexcept;
    // The fill and upgrade queues are still the sole cross-thread payload
    // paths. This nonblocking pipe is only a coalesced wakeup so the worker
    // can sleep in poll() instead of repeatedly rebuilding/scanning a
    // 1,000-fd set while idle. It never carries application data and a full
    // pipe is already a durable wakeup notification.
    void signal_worker() noexcept;
    void drain_worker_signal() noexcept;
    void refresh_poll_interest(Connection& connection) noexcept;
    void clear_poll_interest(Connection& connection) noexcept;
    [[nodiscard]] bool register_connection_io(Connection& connection) noexcept;
    [[nodiscard]] bool set_write_interest(Connection& connection,
                                           bool enabled) noexcept;
    void schedule_flush(Connection& connection) noexcept;
    void flush_queued(uint64_t timestamp_ns) noexcept;
    // Admission words combine a closed bit and an in-flight producer count.
    // A CAS acquires a reference only while the word is open, so stop() can
    // atomically revoke admission and drain every producer that entered
    // before revocation. A load-then-increment lease is not sufficient: it
    // permits a producer preempted between those operations to touch a
    // destroyed server after stop() observes a zero count.
    [[nodiscard]] bool acquire_upgrade_lease() noexcept;
    void release_upgrade_lease() noexcept;
    [[nodiscard]] bool acquire_publish_lease() noexcept;
    void release_publish_lease() noexcept;
    static void close_admission(std::atomic<uint32_t>& admission) noexcept;
    static void open_admission(std::atomic<uint32_t>& admission) noexcept;
    static void wait_for_admission_drain(
        const std::atomic<uint32_t>& admission) noexcept;
    [[nodiscard]] static bool acquire_admission(
        std::atomic<uint32_t>& admission) noexcept;
    static void release_admission(std::atomic<uint32_t>& admission) noexcept;

    StaticSpscQueue<Upgrade, kUpgradeQueueCapacity> upgrades_{};
    StaticSpscQueue<PendingFill, 1024> events_{};
    // Borrowed, caller-owned contiguous storage. It deliberately remains a
    // plain Connection* because subscription indices use pointer arithmetic.
    Connection* connections_ = nullptr;
    std::span<std::byte> connection_storage_{};
    uint32_t constructed_connections_ = 0;
    std::array<SubscriptionBucket, kSubscriptionBuckets> subscriptions_{};
    // Worker-written, release-published diagnostics for burst P99 tests.
    std::array<uint64_t, kLatencySampleCapacity> broadcast_latency_samples_{};
    std::atomic<bool> running_{false};
    static constexpr uint32_t kAdmissionClosedBit = 1U << 31U;
    static constexpr uint32_t kAdmissionRefMask = ~kAdmissionClosedBit;
    // Both paths begin closed. start() opens them only after its worker and
    // fixed storage are fully live; stop() closes both before draining.
    std::atomic<uint32_t> upgrade_admission_{kAdmissionClosedBit};
    std::atomic<uint32_t> publish_admission_{kAdmissionClosedBit};
    std::thread thread_;
    int wake_read_fd_ = -1;
    int wake_write_fd_ = -1;
    // macOS uses kqueue to wait on only ready descriptors. Other POSIX
    // targets retain the fixed poll fallback below; both paths are bounded
    // and allocation-free.
    int event_queue_fd_ = -1;
    std::atomic<bool> worker_wakeup_pending_{false};
    // Worker-owned persistent poll set. Fixed slot index `n + 1` belongs to
    // connection slot `n`; inactive entries use fd=-1. Therefore a fill wake
    // never rebuilds or zero-initializes a 1,000-entry set before it can
    // queue outbound JSON.
    std::array<pollfd, kMaxConnections + 1U> poll_fds_{};
    int32_t flush_head_ = -1;
    int32_t flush_tail_ = -1;
    uint64_t next_maintenance_ns_ = 0;
    uint64_t last_fill_activity_ns_ = 0;
    std::atomic<uint64_t> dropped_{0};
    std::atomic<uint32_t> active_{0};
    // A slot is reserved before enqueue_upgrade reports kAccepted. This
    // prevents pending HTTP upgrades from oversubscribing the fixed pool.
    std::atomic<uint32_t> reserved_{0};
    std::atomic<uint64_t> completed_broadcasts_{0};
    std::atomic<uint64_t> last_broadcast_latency_ns_{0};
    std::atomic<uint32_t> last_fanout_count_{0};
    std::atomic<uint64_t> queued_frames_{0};
    std::atomic<uint64_t> dropped_frames_{0};
    std::atomic<uint64_t> slow_client_closes_{0};
    std::atomic<uint64_t> last_backpressure_close_ns_{0};
    std::atomic<uint64_t> pings_sent_{0};
    std::atomic<uint64_t> pongs_received_{0};
    std::atomic<uint64_t> last_pong_rtt_ns_{0};
    std::atomic<uint64_t> cleanup_count_{0};
    uint64_t ping_interval_ns_;
    uint64_t pong_timeout_ns_;
    uint32_t max_connections_;
};

inline constexpr size_t Server::connection_storage_bytes() noexcept {
    return sizeof(Connection);
}

inline constexpr size_t Server::connection_storage_alignment() noexcept {
    return alignof(Connection);
}

inline constexpr size_t Server::connection_pool_storage_bytes(
    const uint32_t connection_count) noexcept {
    return connection_count <= kMaxConnections
        ? sizeof(Connection) * static_cast<size_t>(connection_count)
        : 0;
}

}  // namespace luv::ws
