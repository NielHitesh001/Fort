#pragma once

// LUV telemetry bridge.
//
// Engine threads publish TelemSnapshot structs through arena.telem_ring.
// A background bridge thread consumes that SPSC ring and sends compact
// heartbeat packets to the dashboard over UDP. No network I/O happens on the
// trading hot path.

#include <atomic>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "luv_arena.hpp"

namespace luv {

enum class TelemetryWireFormat : uint8_t {
    kJson = 0,
    kBinary = 1,
};

struct TelemetryBridgeConfig {
    const char* host = "127.0.0.1";
    uint16_t port = 7777;
    TelemetryWireFormat format = TelemetryWireFormat::kJson;
    uint32_t max_batch = 256;
    uint32_t idle_sleep_us = 1000;
};

struct TelemetryPacket {
    uint32_t magic = 0x3156554C; // "LUV1" little-endian
    uint16_t version = 1;
    uint16_t bytes = sizeof(TelemetryPacket);
    uint64_t sequence = 0;
    TelemSnapshot snapshot {};
};
static_assert(sizeof(TelemetryPacket) == 192,
              "TelemetryPacket wire size changed");

class TelemetryPublisher {
public:
    [[nodiscard]] static uint64_t dropped(const Arena& arena) noexcept {
        return arena.telemetry_drops.load(std::memory_order_relaxed);
    }

    [[nodiscard]] static bool publish_heartbeat(
        Arena& arena,
        uint32_t tick_rate_hz,
        float inference_us,
        float risk_ns) noexcept
    {
        TelemSnapshot* slot = arena.telem_ring.try_claim();
        if (!slot) [[unlikely]] {
            arena.telemetry_drops.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        TelemSnapshot snap {};
        snap.timestamp_ns = now_ns();
        snap.tick_rate_hz = tick_rate_hz;
        snap.inference_us = inference_us;
        snap.risk_ns = risk_ns;

        for (uint32_t sym = 0; sym < Config::kSymbols; ++sym) {
            const SymbolExecState& exec = arena.exec_states[sym];
            snap.session_pnl += exec.risk.daily_pnl;
            snap.gross_exposure += exec.risk.gross_exposure;
            snap.reject_count += static_cast<int32_t>(exec.risk.reject_count);
            snap.active_orders += exec.risk.order_count;
            snap.halted |= exec.risk.halted;

            for (uint32_t i = 0; i < Config::kMaxActiveOrders; ++i) {
                if (exec.orders[i].filled_qty > 0) ++snap.fill_count;
            }
        }

        *slot = snap;
        arena.telem_ring.commit();
        return true;
    }

    [[nodiscard]] static bool publish(Arena& arena,
                                      const TelemSnapshot& snapshot) noexcept
    {
        TelemSnapshot* slot = arena.telem_ring.try_claim();
        if (!slot) [[unlikely]] {
            arena.telemetry_drops.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        *slot = snapshot;
        arena.telem_ring.commit();
        return true;
    }

private:
    [[nodiscard]] static uint64_t now_ns() noexcept {
        struct timespec ts {};
        ::clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
             + static_cast<uint64_t>(ts.tv_nsec);
    }
};

struct TelemetryBatch {
    uint64_t timestamp_ns = 0;
    uint32_t orders_sent = 0;
    uint32_t orders_approved = 0;
    uint32_t orders_rejected = 0;
    uint32_t orders_acked = 0;
    uint32_t orders_filled = 0;
    uint32_t orders_canceled = 0;
    int64_t position = 0;
    int64_t notional = 0;
    uint64_t latency_p50_ns = 0;
    uint64_t latency_p99_ns = 0;
    uint64_t latency_p999_ns = 0;
};

template <uint32_t MaxLatencies = 10'000>
class TelemetryBatchCollector {
public:
    static_assert(MaxLatencies > 0, "Telemetry latency capacity must be non-zero");

    void record_order_sent() noexcept { ++_batch.orders_sent; }
    void record_order_check(bool approved) noexcept {
        if (approved) ++_batch.orders_approved;
        else ++_batch.orders_rejected;
    }
    void record_ack() noexcept { ++_batch.orders_acked; }
    void record_fill(int64_t notional) noexcept {
        ++_batch.orders_filled;
        _batch.notional += notional;
    }
    void record_cancellation() noexcept { ++_batch.orders_canceled; }
    void set_position(int64_t position) noexcept { _batch.position = position; }

    void record_latency(uint64_t latency_ns) noexcept {
        if (_latency_count < MaxLatencies)
            _latencies[_latency_count++] = latency_ns;
        else
            ++_dropped_latencies;
    }

    [[nodiscard]] bool flush(Arena& arena) noexcept {
        TelemSnapshot* slot = arena.telem_ring.try_claim();
        if (!slot) {
            arena.telemetry_drops.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        std::sort(_latencies.begin(), _latencies.begin() + _latency_count);
        _batch.timestamp_ns = now_ns();
        _batch.latency_p50_ns = percentile(50);
        _batch.latency_p99_ns = percentile(99);
        _batch.latency_p999_ns = percentile(999);

        TelemSnapshot snapshot{};
        snapshot.timestamp_ns = _batch.timestamp_ns;
        snapshot.session_pnl = _batch.notional;
        snapshot.gross_exposure = _batch.notional;
        snapshot.fill_count = static_cast<int32_t>(_batch.orders_filled);
        snapshot.reject_count = static_cast<int32_t>(_batch.orders_rejected);
        const uint32_t closed = _batch.orders_filled + _batch.orders_canceled;
        snapshot.active_orders = closed < _batch.orders_approved
                               ? _batch.orders_approved - closed
                               : 0;
        snapshot.inference_us = static_cast<float>(_batch.latency_p50_ns) / 1000.0f;
        snapshot.risk_ns = static_cast<float>(_batch.latency_p99_ns) / 1000.0f;
        *slot = snapshot;
        arena.telem_ring.commit();
        _batch = TelemetryBatch{};
        _latency_count = 0;
        return true;
    }

    [[nodiscard]] const TelemetryBatch& batch() const noexcept { return _batch; }
    [[nodiscard]] uint32_t dropped_latencies() const noexcept {
        return _dropped_latencies;
    }

private:
    [[nodiscard]] uint64_t percentile(uint32_t rank) const noexcept {
        if (_latency_count == 0) return 0;
        const uint32_t denominator = rank >= 1000 ? 1000 : 100;
        const uint64_t index =
            (static_cast<uint64_t>(_latency_count) * rank + denominator - 1) /
            denominator;
        return _latencies[static_cast<size_t>(
            index == 0 ? 0 : std::min(index - 1,
                                      static_cast<uint64_t>(_latency_count - 1)))];
    }

    [[nodiscard]] static uint64_t now_ns() noexcept {
        struct timespec ts{};
        ::clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
               static_cast<uint64_t>(ts.tv_nsec);
    }

    TelemetryBatch _batch{};
    std::array<uint64_t, MaxLatencies> _latencies{};
    uint32_t _latency_count = 0;
    uint32_t _dropped_latencies = 0;
};

class PrometheusMetricsExporter {
public:
    static std::string render(const TelemSnapshot& snap) noexcept {
        char buf[1024];
        const int n = std::snprintf(
            buf,
            sizeof(buf),
            "# HELP luv_session_pnl Session PnL\n"
            "# TYPE luv_session_pnl gauge\n"
            "luv_session_pnl %lld\n"
            "# HELP luv_gross_exposure Gross exposure\n"
            "# TYPE luv_gross_exposure gauge\n"
            "luv_gross_exposure %lld\n"
            "# HELP luv_fill_count Total fills observed\n"
            "# TYPE luv_fill_count counter\n"
            "luv_fill_count %d\n"
            "# HELP luv_reject_count Total rejected orders\n"
            "# TYPE luv_reject_count counter\n"
            "luv_reject_count %d\n"
            "# HELP luv_tick_rate_hz Tick rate in hertz\n"
            "# TYPE luv_tick_rate_hz gauge\n"
            "luv_tick_rate_hz %u\n"
            "# HELP luv_active_orders Active order count\n"
            "# TYPE luv_active_orders gauge\n"
            "luv_active_orders %u\n"
            "# HELP luv_inference_us Inference latency in microseconds\n"
            "# TYPE luv_inference_us gauge\n"
            "luv_inference_us %.3f\n"
            "# HELP luv_risk_ns Risk check latency in nanoseconds\n"
            "# TYPE luv_risk_ns gauge\n"
            "luv_risk_ns %.3f\n",
            static_cast<long long>(snap.session_pnl),
            static_cast<long long>(snap.gross_exposure),
            snap.fill_count,
            snap.reject_count,
            snap.tick_rate_hz,
            snap.active_orders,
            static_cast<double>(snap.inference_us),
            static_cast<double>(snap.risk_ns));
        if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf)) return {};
        return std::string(buf, static_cast<size_t>(n));
    }
};

class MetricsHttpServer {
public:
    explicit MetricsHttpServer(uint16_t port = 9090) noexcept
        : _requested_port(port) {}

    MetricsHttpServer(const MetricsHttpServer&) = delete;
    MetricsHttpServer& operator=(const MetricsHttpServer&) = delete;

    ~MetricsHttpServer() { stop(); }

    [[nodiscard]] bool start(const TelemSnapshot& initial) {
        {
            std::lock_guard<std::mutex> lock(_snapshot_mutex);
            _snapshot = initial;
        }

        _fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (_fd < 0) return false;

        int reuse = 1;
        ::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(_requested_port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
            ::listen(_fd, 8) < 0) {
            close_socket();
            return false;
        }

        socklen_t length = sizeof(addr);
        if (::getsockname(_fd, reinterpret_cast<sockaddr*>(&addr), &length) < 0) {
            close_socket();
            return false;
        }
        _bound_port = ntohs(addr.sin_port);
        _running.store(true, std::memory_order_release);
        _thread = std::thread(&MetricsHttpServer::serve, this);
        return true;
    }

    void update(const TelemSnapshot& snapshot) noexcept {
        std::lock_guard<std::mutex> lock(_snapshot_mutex);
        _snapshot = snapshot;
    }

    void stop() noexcept {
        if (!_running.exchange(false, std::memory_order_acq_rel)) return;
        close_socket();
        if (_thread.joinable()) _thread.join();
    }

    [[nodiscard]] uint16_t port() const noexcept { return _bound_port; }

private:
    void serve() noexcept {
        while (_running.load(std::memory_order_acquire)) {
            const int client = ::accept(_fd, nullptr, nullptr);
            if (client < 0) continue;

            char request[512]{};
            const ssize_t received = ::recv(client, request, sizeof(request) - 1, 0);
            const bool metrics_request = received > 0 &&
                std::strncmp(request, "GET /metrics ", 13) == 0;

            if (metrics_request) {
                TelemSnapshot snapshot{};
                {
                    std::lock_guard<std::mutex> lock(_snapshot_mutex);
                    snapshot = _snapshot;
                }
                const std::string body = PrometheusMetricsExporter::render(snapshot);
                const std::string response =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain; version=0.0.4\r\n"
                    "Content-Length: " + std::to_string(body.size()) +
                    "\r\n\r\n" + body;
                send_all(client, response.data(), response.size());
            } else {
                constexpr const char response[] =
                    "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                send_all(client, response, sizeof(response) - 1);
            }
            ::close(client);
        }
    }

    static void send_all(int fd, const char* data, size_t bytes) noexcept {
        while (bytes > 0) {
            const ssize_t sent = ::send(fd, data, bytes, 0);
            if (sent <= 0) return;
            data += sent;
            bytes -= static_cast<size_t>(sent);
        }
    }

    void close_socket() noexcept {
        if (_fd >= 0) {
            ::shutdown(_fd, SHUT_RDWR);
            ::close(_fd);
            _fd = -1;
        }
    }

    uint16_t _requested_port = 9090;
    uint16_t _bound_port = 0;
    int _fd = -1;
    std::atomic<bool> _running{false};
    std::thread _thread;
    std::mutex _snapshot_mutex;
    TelemSnapshot _snapshot{};
};

struct HealthCheckResult {
    bool feed_fresh = false;
    bool lob_consistent = false;
    bool execution_backlog = false;
    bool memory_ok = false;
    bool disk_space_ok = false;
    bool cpu_usage_ok = false;
};

inline HealthCheckResult run_health_checks(
    const TelemSnapshot& snapshot,
    uint64_t last_feed_msg_ns,
    uint64_t active_orders,
    uint64_t rss_bytes,
    uint64_t available_disk_bytes,
    double cpu_usage_pct,
    uint32_t backlog_limit,
    uint64_t memory_limit_bytes,
    uint64_t disk_limit_bytes,
    double cpu_limit_pct) noexcept
{
    const uint64_t now_ns = []() noexcept {
        struct timespec ts{};
        ::clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
               static_cast<uint64_t>(ts.tv_nsec);
    }();

    const uint64_t feed_age_ns = now_ns > last_feed_msg_ns ? now_ns - last_feed_msg_ns : 0;
    const bool feed_fresh = feed_age_ns < 100'000'000ULL;
    const bool lob_consistent = snapshot.halted == 0 && snapshot.active_orders >= 0;
    const bool execution_backlog = active_orders <= backlog_limit;
    const bool memory_ok = rss_bytes <= memory_limit_bytes;
    const bool disk_space_ok = available_disk_bytes >= disk_limit_bytes;
    const bool cpu_usage_ok = cpu_usage_pct <= cpu_limit_pct;

    return {feed_fresh, lob_consistent, execution_backlog,
            memory_ok, disk_space_ok, cpu_usage_ok};
}

class TelemetryBridge {
public:
    TelemetryBridge() = default;
    TelemetryBridge(const TelemetryBridge&) = delete;
    TelemetryBridge& operator=(const TelemetryBridge&) = delete;

    ~TelemetryBridge() { stop(); close_socket(); }

    [[nodiscard]] bool init(Arena& arena,
                            const TelemetryBridgeConfig& cfg) noexcept
    {
        if (!arena.is_initialised() || !cfg.host || cfg.port == 0)
            return false;

        _arena = &arena;
        _cfg = cfg;
        if (_cfg.max_batch == 0) _cfg.max_batch = 1;

        _fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (_fd < 0) return false;

        std::memset(&_dst, 0, sizeof(_dst));
        _dst.sin_family = AF_INET;
        _dst.sin_port = htons(_cfg.port);
        if (::inet_pton(AF_INET, _cfg.host, &_dst.sin_addr) != 1) {
            close_socket();
            return false;
        }

        return true;
    }

    [[nodiscard]] bool start() noexcept {
        if (!_arena || _fd < 0 || _running.load(std::memory_order_relaxed))
            return false;

        _running.store(true, std::memory_order_relaxed);
        try {
            _thread = std::thread([this] { run_loop(); });
        } catch (...) {
            _running.store(false, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    void stop() noexcept {
        _running.store(false, std::memory_order_relaxed);
        if (_thread.joinable()) _thread.join();
    }

    [[nodiscard]] uint32_t pump_once() noexcept {
        if (!_arena || _fd < 0) return 0;

        uint32_t sent = 0;
        while (sent < _cfg.max_batch) {
            TelemSnapshot* snap = _arena->telem_ring.try_peek();
            if (!snap) break;

            if (send_snapshot(*snap)) {
                ++sent;
                ++_snapshots_sent;
            } else {
                ++_send_errors;
            }
            _arena->telem_ring.consume();
        }
        return sent;
    }

    [[nodiscard]] uint64_t snapshots_sent() const noexcept {
        return _snapshots_sent;
    }

    [[nodiscard]] uint64_t send_errors() const noexcept {
        return _send_errors;
    }

private:
    void run_loop() noexcept {
        while (_running.load(std::memory_order_relaxed)) {
            const uint32_t n = pump_once();
            if (n == 0) {
                std::this_thread::sleep_for(
                    std::chrono::microseconds(_cfg.idle_sleep_us));
            }
        }

        while (pump_once() != 0) {}
    }

    [[nodiscard]] bool send_snapshot(const TelemSnapshot& snap) noexcept {
        if (_cfg.format == TelemetryWireFormat::kBinary) {
            TelemetryPacket packet {};
            packet.sequence = _sequence++;
            packet.snapshot = snap;
            return send_bytes(&packet, sizeof(packet));
        }

        char buf[512];
        const int n = std::snprintf(
            buf,
            sizeof(buf),
            "{\"type\":\"luv_heartbeat\",\"seq\":%llu,"
            "\"timestamp_ns\":%llu,\"session_pnl\":%lld,"
            "\"gross_exposure\":%lld,\"fill_count\":%d,"
            "\"reject_count\":%d,\"tick_rate_hz\":%u,"
            "\"active_orders\":%u,\"inference_us\":%.3f,"
            "\"risk_ns\":%.3f,\"halted\":%u}\n",
            static_cast<unsigned long long>(_sequence++),
            static_cast<unsigned long long>(snap.timestamp_ns),
            static_cast<long long>(snap.session_pnl),
            static_cast<long long>(snap.gross_exposure),
            snap.fill_count,
            snap.reject_count,
            snap.tick_rate_hz,
            snap.active_orders,
            static_cast<double>(snap.inference_us),
            static_cast<double>(snap.risk_ns),
            static_cast<unsigned>(snap.halted)
        );

        if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf)) return false;
        return send_bytes(buf, static_cast<size_t>(n));
    }

    [[nodiscard]] bool send_bytes(const void* data, size_t bytes) noexcept {
        const ssize_t rc = ::sendto(
            _fd,
            data,
            bytes,
            0,
            reinterpret_cast<const sockaddr*>(&_dst),
            sizeof(_dst)
        );
        return rc == static_cast<ssize_t>(bytes);
    }

    void close_socket() noexcept {
        if (_fd >= 0) {
            ::close(_fd);
            _fd = -1;
        }
    }

    Arena* _arena = nullptr;
    TelemetryBridgeConfig _cfg {};
    int _fd = -1;
    sockaddr_in _dst {};
    std::atomic<bool> _running {false};
    std::thread _thread;
    uint64_t _sequence = 0;
    uint64_t _snapshots_sent = 0;
    uint64_t _send_errors = 0;
};

}  // namespace luv
