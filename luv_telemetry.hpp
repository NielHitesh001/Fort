#pragma once

// LUV telemetry bridge.
//
// Engine threads publish TelemSnapshot structs through arena.telem_ring.
// One background sink (the UDP bridge or the Prometheus HTTP worker) consumes
// that SPSC ring.  No network I/O happens on the trading hot path.

#include <atomic>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
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
        else {
            ++_batch.orders_rejected;
            ++_rejections_total;
        }
    }
    void record_ack() noexcept { ++_batch.orders_acked; }
    void record_fill(int64_t notional) noexcept {
        ++_batch.orders_filled;
        ++_fills_total;
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
        snapshot.fills_total = _fills_total;
        snapshot.rejections_total = _rejections_total;
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
    uint64_t _fills_total = 0;
    uint64_t _rejections_total = 0;
};

class PrometheusMetricsExporter {
public:
    // Includes the primary execution schema plus the legacy Grafana aliases.
    // It is fixed storage used by the worker; no response construction needs
    // a dynamic string.
    static constexpr size_t kMaxRenderedBytes = 4096;

    // Writes the Prometheus exposition directly into caller-owned storage.
    // The metrics HTTP worker uses this function so scrape handling never
    // allocates.  Snapshot fill/reject observations remain gauges, while the
    // producer-owned 64-bit fields below are exported as monotonic counters.
    [[nodiscard]] static size_t render_into(char* out,
                                            size_t capacity,
                                            const TelemSnapshot& snap,
                                            uint64_t telemetry_queue_depth,
                                            uint64_t telemetry_dropped) noexcept {
        if (!out || capacity == 0) return 0;
        const int n = std::snprintf(
            out,
            capacity,
            "# HELP luv_execution_session_pnl Session PnL in fixed-point units\n"
            "# TYPE luv_execution_session_pnl gauge\n"
            "luv_execution_session_pnl %lld\n"
            "# HELP luv_execution_gross_exposure Gross exposure in fixed-point units\n"
            "# TYPE luv_execution_gross_exposure gauge\n"
            "luv_execution_gross_exposure %lld\n"
            "# HELP luv_execution_fills_observed Fills in the most recent telemetry observation\n"
            "# TYPE luv_execution_fills_observed gauge\n"
            "luv_execution_fills_observed %d\n"
            "# HELP luv_execution_rejections_observed Rejections in the most recent telemetry observation\n"
            "# TYPE luv_execution_rejections_observed gauge\n"
            "luv_execution_rejections_observed %d\n"
            "# HELP luv_execution_fills_total Cumulative reconciled execution fill reports\n"
            "# TYPE luv_execution_fills_total counter\n"
            "luv_execution_fills_total %llu\n"
            "# HELP luv_execution_rejections_total Cumulative execution rejections\n"
            "# TYPE luv_execution_rejections_total counter\n"
            "luv_execution_rejections_total %llu\n"
            "# HELP luv_websocket_fill_notification_drops_total Fill notifications dropped because the bounded WebSocket SPSC queue was full\n"
            "# TYPE luv_websocket_fill_notification_drops_total counter\n"
            "luv_websocket_fill_notification_drops_total %llu\n"
            "# HELP luv_execution_tick_rate_hz Tick processing rate in hertz\n"
            "# TYPE luv_execution_tick_rate_hz gauge\n"
            "luv_execution_tick_rate_hz %u\n"
            "# HELP luv_execution_active_orders Active order count\n"
            "# TYPE luv_execution_active_orders gauge\n"
            "luv_execution_active_orders %u\n"
            "# HELP luv_execution_halted Execution halt state; 1 means halted\n"
            "# TYPE luv_execution_halted gauge\n"
            "luv_execution_halted %u\n"
            "# HELP luv_execution_inference_latency_microseconds Last inference latency\n"
            "# TYPE luv_execution_inference_latency_microseconds gauge\n"
            "luv_execution_inference_latency_microseconds %.3f\n"
            "# HELP luv_execution_risk_check_latency_nanoseconds Last risk check latency\n"
            "# TYPE luv_execution_risk_check_latency_nanoseconds gauge\n"
            "luv_execution_risk_check_latency_nanoseconds %.3f\n"
            "# HELP luv_session_pnl Deprecated alias for luv_execution_session_pnl\n"
            "# TYPE luv_session_pnl gauge\n"
            "luv_session_pnl %lld\n"
            "# HELP luv_gross_exposure Deprecated alias for luv_execution_gross_exposure\n"
            "# TYPE luv_gross_exposure gauge\n"
            "luv_gross_exposure %lld\n"
            "# HELP luv_fill_count Deprecated observation gauge, not a cumulative counter\n"
            "# TYPE luv_fill_count gauge\n"
            "luv_fill_count %d\n"
            "# HELP luv_reject_count Deprecated observation gauge, not a cumulative counter\n"
            "# TYPE luv_reject_count gauge\n"
            "luv_reject_count %d\n"
            "# HELP luv_tick_rate_hz Deprecated alias for luv_execution_tick_rate_hz\n"
            "# TYPE luv_tick_rate_hz gauge\n"
            "luv_tick_rate_hz %u\n"
            "# HELP luv_active_orders Deprecated alias for luv_execution_active_orders\n"
            "# TYPE luv_active_orders gauge\n"
            "luv_active_orders %u\n"
            "# HELP luv_inference_us Deprecated alias for inference latency\n"
            "# TYPE luv_inference_us gauge\n"
            "luv_inference_us %.3f\n"
            "# HELP luv_risk_ns Deprecated alias for risk check latency\n"
            "# TYPE luv_risk_ns gauge\n"
            "luv_risk_ns %.3f\n"
            "# HELP luv_telemetry_queue_depth Pending telemetry snapshots\n"
            "# TYPE luv_telemetry_queue_depth gauge\n"
            "luv_telemetry_queue_depth %llu\n"
            "# HELP luv_telemetry_dropped_snapshots_total Snapshots dropped because the SPSC ring was full\n"
            "# TYPE luv_telemetry_dropped_snapshots_total counter\n"
            "luv_telemetry_dropped_snapshots_total %llu\n",
            static_cast<long long>(snap.session_pnl),
            static_cast<long long>(snap.gross_exposure),
            snap.fill_count,
            snap.reject_count,
            static_cast<unsigned long long>(snap.fills_total),
            static_cast<unsigned long long>(snap.rejections_total),
            static_cast<unsigned long long>(
                snap.websocket_fill_notification_drops_total),
            snap.tick_rate_hz,
            snap.active_orders,
            static_cast<unsigned>(snap.halted != 0),
            static_cast<double>(snap.inference_us),
            static_cast<double>(snap.risk_ns),
            static_cast<long long>(snap.session_pnl),
            static_cast<long long>(snap.gross_exposure),
            snap.fill_count,
            snap.reject_count,
            snap.tick_rate_hz,
            snap.active_orders,
            static_cast<double>(snap.inference_us),
            static_cast<double>(snap.risk_ns),
            static_cast<unsigned long long>(telemetry_queue_depth),
            static_cast<unsigned long long>(telemetry_dropped));
        if (n <= 0 || static_cast<size_t>(n) >= capacity) return 0;
        return static_cast<size_t>(n);
    }

    // Compatibility helper for callers outside the hot path (for example,
    // diagnostics tests).  The HTTP worker intentionally does not call this.
    [[nodiscard]] static std::string render(const TelemSnapshot& snap) noexcept {
        std::array<char, kMaxRenderedBytes> buffer{};
        const size_t bytes = render_into(buffer.data(), buffer.size(), snap, 0, 0);
        if (bytes == 0) return {};
        try {
            return std::string(buffer.data(), bytes);
        } catch (...) {
            return {};
        }
    }
};

class MetricsHttpServer {
public:
    static constexpr size_t kMaxBearerTokenBytes = 256;

    // The server binds INADDR_ANY by default so a container Service can
    // reach it.  `arena` must remain alive until stop() returns.  This server
    // is the sole consumer of arena.telem_ring while it is running.
    explicit MetricsHttpServer(uint16_t port = 9090) noexcept
        : _requested_port(port) {}

    MetricsHttpServer(const MetricsHttpServer&) = delete;
    MetricsHttpServer& operator=(const MetricsHttpServer&) = delete;

    ~MetricsHttpServer() { stop(); }

    [[nodiscard]] bool start(Arena& arena,
                             const TelemSnapshot& initial,
                             const char* bearer_token) noexcept {
        if (!arena.is_initialised() || _running.load(std::memory_order_acquire) ||
            _fd >= 0 || !copy_token(bearer_token)) {
            return false;
        }

        _fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (_fd < 0) return false;

        int reuse = 1;
        ::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (!set_nonblocking(_fd)) {
            close_listener();
            return false;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(_requested_port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (::bind(_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
            ::listen(_fd, 8) < 0) {
            close_listener();
            return false;
        }

        socklen_t length = sizeof(addr);
        if (::getsockname(_fd, reinterpret_cast<sockaddr*>(&addr), &length) < 0) {
            close_listener();
            return false;
        }
        _bound_port = ntohs(addr.sin_port);
        _bound_address = addr.sin_addr.s_addr;
        _arena = &arena;
        _snapshot = initial;
        _running.store(true, std::memory_order_release);
        try {
            _thread = std::thread(&MetricsHttpServer::serve, this);
        } catch (...) {
            _running.store(false, std::memory_order_release);
            _arena = nullptr;
            close_listener();
            return false;
        }
        return true;
    }

    void stop() noexcept {
        if (!_running.exchange(false, std::memory_order_acq_rel)) return;
        if (_thread.joinable()) _thread.join();
        close_listener();
        _arena = nullptr;
    }

    [[nodiscard]] uint16_t port() const noexcept { return _bound_port; }
    [[nodiscard]] in_addr_t bound_address() const noexcept {
        return _bound_address;
    }

private:
    static constexpr int kListenerPollMs = 20;
    static constexpr int kClientIoTimeoutMs = 100;
    static constexpr uint32_t kMaxSnapshotsPerLoop = 1024;
    static constexpr size_t kRequestBytes = 2048;

    [[nodiscard]] static uint64_t monotonic_now_ms() noexcept {
        timespec ts{};
        (void)::clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
               static_cast<uint64_t>(ts.tv_nsec) / 1'000'000ULL;
    }

    [[nodiscard]] static int remaining_timeout_ms(uint64_t deadline_ms) noexcept {
        const uint64_t now_ms = monotonic_now_ms();
        if (now_ms >= deadline_ms) return 0;
        const uint64_t remaining = deadline_ms - now_ms;
        return remaining > static_cast<uint64_t>(kClientIoTimeoutMs)
            ? kClientIoTimeoutMs : static_cast<int>(remaining);
    }

    void serve() noexcept {
        while (_running.load(std::memory_order_acquire)) {
            drain_snapshots();

            pollfd listener{};
            listener.fd = _fd;
            listener.events = POLLIN;
            const int ready = ::poll(&listener, 1, kListenerPollMs);
            if (ready <= 0 || (listener.revents & POLLIN) == 0) continue;

            while (_running.load(std::memory_order_acquire)) {
                const int client = ::accept(_fd, nullptr, nullptr);
                if (client < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    break;
                }
                if (set_nonblocking(client)) service_client(client);
                ::close(client);
            }
        }

        // Publish the newest queued state before shutdown.  This does not
        // attempt to drain forever if a producer is still racing shutdown.
        drain_snapshots();
    }

    void drain_snapshots() noexcept {
        if (!_arena) return;
        uint32_t consumed = 0;
        while (consumed < kMaxSnapshotsPerLoop) {
            TelemSnapshot* snap = _arena->telem_ring.try_peek();
            if (!snap) break;
            _snapshot = *snap;
            _arena->telem_ring.consume();
            ++consumed;
        }
    }

    void service_client(int client) noexcept {
        size_t bytes = 0;
        if (!receive_headers(client, _request_buffer.data(), _request_buffer.size(), bytes)) {
            return;
        }

        const RequestKind kind = classify_request(_request_buffer.data(), bytes);
        if (kind == RequestKind::kHealth) {
            send_response(client, "200 OK", "text/plain; charset=utf-8", "ok\n", 3,
                          nullptr);
            return;
        }
        if (kind == RequestKind::kMetrics) {
            if (!has_valid_bearer(_request_buffer.data(), bytes)) {
                constexpr char body[] = "Unauthorized\n";
                send_response(client, "401 Unauthorized", "text/plain; charset=utf-8",
                              body, sizeof(body) - 1,
                              "WWW-Authenticate: Bearer realm=\"metrics\"\r\n");
                return;
            }

            drain_snapshots();
            const uint64_t queue_depth = _arena ? _arena->telem_ring.size() : 0;
            const uint64_t dropped = _arena
                ? _arena->telemetry_drops.load(std::memory_order_relaxed) : 0;
            const size_t body_bytes = PrometheusMetricsExporter::render_into(
                _metrics_body.data(), _metrics_body.size(), _snapshot, queue_depth, dropped);
            if (body_bytes == 0) {
                constexpr char error[] = "Metrics rendering failed\n";
                send_response(client, "500 Internal Server Error",
                              "text/plain; charset=utf-8", error, sizeof(error) - 1,
                              nullptr);
                return;
            }
            send_response(client, "200 OK", "text/plain; version=0.0.4",
                          _metrics_body.data(), body_bytes, nullptr);
            return;
        }

        constexpr char body[] = "Not Found\n";
        send_response(client, "404 Not Found", "text/plain; charset=utf-8",
                      body, sizeof(body) - 1, nullptr);
    }

    enum class RequestKind : uint8_t {
        kInvalid,
        kMetrics,
        kHealth,
    };

    [[nodiscard]] static bool receive_headers(int fd,
                                              char* out,
                                              size_t capacity,
                                              size_t& bytes) noexcept {
        bytes = 0;
        const uint64_t deadline_ms = monotonic_now_ms() + kClientIoTimeoutMs;
        while (bytes < capacity) {
            const ssize_t received = ::recv(fd, out + bytes, capacity - bytes,
                                             MSG_DONTWAIT);
            if (received > 0) {
                bytes += static_cast<size_t>(received);
                if (header_complete(out, bytes)) return true;
                continue;
            }
            if (received == 0) return false;
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return false;

            pollfd event{};
            event.fd = fd;
            event.events = POLLIN;
            const int remaining_ms = remaining_timeout_ms(deadline_ms);
            if (remaining_ms <= 0) return false;
            const int ready = ::poll(&event, 1, remaining_ms);
            if (ready <= 0 || (event.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
                return false;
        }
        return false;
    }

    [[nodiscard]] static bool header_complete(const char* data, size_t bytes) noexcept {
        if (bytes < 4) return false;
        for (size_t i = 3; i < bytes; ++i) {
            if (data[i - 3] == '\r' && data[i - 2] == '\n' &&
                data[i - 1] == '\r' && data[i] == '\n') {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static bool ascii_equal_ci(const char* lhs,
                                             size_t lhs_size,
                                             const char* rhs) noexcept {
        const size_t rhs_size = std::strlen(rhs);
        if (lhs_size != rhs_size) return false;
        for (size_t i = 0; i < lhs_size; ++i) {
            char a = lhs[i];
            char b = rhs[i];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) return false;
        }
        return true;
    }

    [[nodiscard]] static RequestKind classify_request(const char* request,
                                                       size_t bytes) noexcept {
        size_t line_end = 0;
        while (line_end + 1 < bytes &&
               !(request[line_end] == '\r' && request[line_end + 1] == '\n')) {
            ++line_end;
        }
        if (line_end + 1 >= bytes) return RequestKind::kInvalid;

        constexpr char metrics_line[] = "GET /metrics HTTP/1.1";
        constexpr char health_line[] = "GET /healthz HTTP/1.1";
        if (line_end == sizeof(metrics_line) - 1 &&
            std::memcmp(request, metrics_line, line_end) == 0) {
            return RequestKind::kMetrics;
        }
        if (line_end == sizeof(health_line) - 1 &&
            std::memcmp(request, health_line, line_end) == 0) {
            return RequestKind::kHealth;
        }
        return RequestKind::kInvalid;
    }

    [[nodiscard]] bool has_valid_bearer(const char* request, size_t bytes) const noexcept {
        size_t line_start = 0;
        while (line_start + 1 < bytes &&
               !(request[line_start] == '\r' && request[line_start + 1] == '\n')) {
            ++line_start;
        }
        if (line_start + 1 >= bytes) return false;
        line_start += 2;

        while (line_start + 1 < bytes) {
            size_t line_end = line_start;
            while (line_end + 1 < bytes &&
                   !(request[line_end] == '\r' && request[line_end + 1] == '\n')) {
                ++line_end;
            }
            if (line_end + 1 >= bytes || line_end == line_start) break;

            size_t colon = line_start;
            while (colon < line_end && request[colon] != ':') ++colon;
            if (colon < line_end && ascii_equal_ci(request + line_start,
                                                   colon - line_start,
                                                   "Authorization")) {
                size_t value = colon + 1;
                while (value < line_end && (request[value] == ' ' || request[value] == '\t'))
                    ++value;
                constexpr char scheme[] = "Bearer ";
                constexpr char bearer[] = "Bearer";
                if (line_end - value < sizeof(scheme) - 1 ||
                    !ascii_equal_ci(request + value, sizeof(bearer) - 1, bearer) ||
                    request[value + sizeof(bearer) - 1] != ' ') {
                    return false;
                }
                value += sizeof(scheme) - 1;
                size_t token_end = line_end;
                while (token_end > value &&
                       (request[token_end - 1] == ' ' || request[token_end - 1] == '\t')) {
                    --token_end;
                }
                return constant_time_token_equal(request + value, token_end - value);
            }
            line_start = line_end + 2;
        }
        return false;
    }

    [[nodiscard]] bool constant_time_token_equal(const char* token,
                                                 size_t token_size) const noexcept {
        uint8_t difference = static_cast<uint8_t>(token_size != _token_size);
        for (size_t i = 0; i < kMaxBearerTokenBytes; ++i) {
            const uint8_t provided = i < token_size
                ? static_cast<uint8_t>(token[i]) : 0;
            const uint8_t expected = i < _token_size
                ? static_cast<uint8_t>(_token[i]) : 0;
            difference |= static_cast<uint8_t>(provided ^ expected);
        }
        return difference == 0;
    }

    [[nodiscard]] static bool set_nonblocking(int fd) noexcept {
        const int flags = ::fcntl(fd, F_GETFL, 0);
        return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    [[nodiscard]] static bool send_all(int fd, const char* data, size_t bytes) noexcept {
        const uint64_t deadline_ms = monotonic_now_ms() + kClientIoTimeoutMs;
        while (bytes > 0) {
            const ssize_t sent = ::send(fd, data, bytes, MSG_DONTWAIT | MSG_NOSIGNAL);
            if (sent > 0) {
                data += sent;
                bytes -= static_cast<size_t>(sent);
                continue;
            }
            if (sent < 0 && errno == EINTR) continue;
            if (sent >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) return false;

            pollfd event{};
            event.fd = fd;
            event.events = POLLOUT;
            constexpr int kWaitSliceMs = 10;
            const int remaining_ms = remaining_timeout_ms(deadline_ms);
            const int slice = remaining_ms < kWaitSliceMs
                ? remaining_ms : kWaitSliceMs;
            if (slice <= 0 || ::poll(&event, 1, slice) <= 0 ||
                (event.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                return false;
            }
        }
        return true;
    }

    void send_response(int fd,
                       const char* status,
                       const char* content_type,
                       const char* body,
                       size_t body_bytes,
                       const char* extra_headers) noexcept {
        const int header_bytes = std::snprintf(
            _response_header.data(), _response_header.size(),
            "HTTP/1.1 %s\r\n"
            "Content-Type: %s\r\n"
            "%s"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n",
            status, content_type, extra_headers ? extra_headers : "", body_bytes);
        if (header_bytes <= 0 ||
            static_cast<size_t>(header_bytes) >= _response_header.size()) {
            return;
        }
        if (!send_all(fd, _response_header.data(), static_cast<size_t>(header_bytes))) return;
        (void)send_all(fd, body, body_bytes);
    }

    [[nodiscard]] bool copy_token(const char* bearer_token) noexcept {
        if (!bearer_token) return false;
        const size_t size = ::strnlen(bearer_token, kMaxBearerTokenBytes + 1U);
        if (size == 0 || size > kMaxBearerTokenBytes) return false;
        _token.fill('\0');
        std::memcpy(_token.data(), bearer_token, size);
        _token_size = size;
        return true;
    }

    void close_listener() noexcept {
        if (_fd >= 0) {
            ::close(_fd);
            _fd = -1;
        }
    }

    uint16_t _requested_port = 9090;
    uint16_t _bound_port = 0;
    in_addr_t _bound_address = htonl(INADDR_ANY);
    int _fd = -1;
    std::atomic<bool> _running{false};
    std::thread _thread;
    Arena* _arena = nullptr;
    TelemSnapshot _snapshot{};
    std::array<char, kMaxBearerTokenBytes> _token{};
    // The worker has one owner, so these fixed buffers can be reused for each
    // scrape without stack growth or heap allocation.
    std::array<char, kRequestBytes> _request_buffer{};
    std::array<char, PrometheusMetricsExporter::kMaxRenderedBytes> _metrics_body{};
    std::array<char, 256> _response_header{};
    size_t _token_size = 0;
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
