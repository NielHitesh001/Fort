// Simulation-only end-to-end LUV topology.
//
// Ingest: SimFeedSource -> arena.tick_ring
// Strategy: Consumer + AI signal -> ExecutionGateway -> packet queue
// Egress: packet queue -> localhost UDP (optional)

#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <climits>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "luv_ai.hpp"
#include "luv_consumer.hpp"
#include "luv_execution.hpp"
#include "luv_feed_sim.hpp"
#include "luv_http_server.hpp"
#include "luv_telemetry.hpp"
#include "luv_websocket.hpp"
#include "luv_safety.hpp"

namespace {

constexpr uint32_t kPacketQueueCapacity = 1u << 12;
// The HTTP request ring holds 256 commands.  This larger, fixed owner-thread
// FIFO keeps a deterministic simulated venue delay without allocating or
// blocking the execution owner under a short control-plane burst.
constexpr uint32_t kSimulatedRestFillCapacity = 1u << 12;
constexpr uint32_t kMaxHttpRequestsPerExecutionTurn = 16;
constexpr uint64_t kSimulatedRestFillDelayNs = 25'000'000ULL;
// Spread a burst after the common 25 ms venue delay. This keeps the
// execution-to-WebSocket SPSC producer bounded below its 1024-event capacity
// without ever sleeping or blocking the execution owner.
constexpr uint64_t kSimulatedRestFillSpacingNs = 25'000ULL;

struct RunConfig {
    uint64_t message_limit = 100'000;
    uint16_t udp_port = 0;  // 0 keeps egress disabled.
    uint16_t http_port = 0; // 0 keeps the control-plane API disabled.
    uint16_t prometheus_port = 0; // 0 keeps the metrics endpoint disabled.
    const char* model_path = nullptr;
    const char* api_token = nullptr;
    const char* api_token_file = nullptr;
    const char* metrics_token = nullptr;
    const char* metrics_token_file = nullptr;
};

[[nodiscard]] bool parse_args(int argc, char** argv, RunConfig& cfg) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--messages") == 0 && i + 1 < argc) {
            cfg.message_limit = std::strtoull(argv[++i], nullptr, 10);
        } else if (std::strcmp(argv[i], "--udp-port") == 0 && i + 1 < argc) {
            cfg.udp_port = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            cfg.model_path = argv[++i];
        } else if (std::strcmp(argv[i], "--http-port") == 0 && i + 1 < argc) {
            cfg.http_port = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--prometheus-port") == 0 && i + 1 < argc) {
            cfg.prometheus_port = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (std::strcmp(argv[i], "--api-token") == 0 && i + 1 < argc) {
            cfg.api_token = argv[++i];
        } else if (std::strcmp(argv[i], "--api-token-file") == 0 &&
                   i + 1 < argc) {
            cfg.api_token_file = argv[++i];
        } else if (std::strcmp(argv[i], "--metrics-token") == 0 &&
                   i + 1 < argc) {
            cfg.metrics_token = argv[++i];
        } else if (std::strcmp(argv[i], "--metrics-token-file") == 0 &&
                   i + 1 < argc) {
            cfg.metrics_token_file = argv[++i];
        } else {
            return false;
        }
    }
    const bool api_credential =
        cfg.api_token != nullptr || cfg.api_token_file != nullptr;
    const bool metrics_credential =
        cfg.metrics_token != nullptr || cfg.metrics_token_file != nullptr;
    return cfg.message_limit != 0 &&
           (cfg.http_port == 0 || api_credential) &&
           (cfg.prometheus_port == 0 || api_credential || metrics_credential) &&
           !(cfg.api_token != nullptr && cfg.api_token_file != nullptr) &&
           !(cfg.metrics_token != nullptr &&
             cfg.metrics_token_file != nullptr);
}

void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#endif
}

[[nodiscard]] uint64_t monotonic_now_ns() noexcept {
    timespec ts {};
    (void)::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

[[nodiscard]] uint64_t utc_now_ns() noexcept {
    timespec ts{};
    (void)::clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

// Kubernetes Secret volumes contain the decoded token in a small regular
// file. Read it once at start-up into a bounded buffer; no token value is put
// on the process command line or copied into an environment variable.
[[nodiscard]] bool load_api_token_file(const char* path,
                                       char* output,
                                       size_t output_size) noexcept {
    if (!path || !output || output_size < 2U) return false;
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    const ssize_t bytes = ::read(fd, output, output_size);
    (void)::close(fd);
    if (bytes <= 0 || static_cast<size_t>(bytes) >= output_size) return false;

    size_t length = static_cast<size_t>(bytes);
    while (length != 0 &&
           (output[length - 1U] == '\n' || output[length - 1U] == '\r')) {
        --length;
    }
    if (length == 0) return false;
    for (size_t index = 0; index < length; ++index) {
        if (output[index] == '\0') return false;
    }
    output[length] = '\0';
    return true;
}

struct PendingSimulatedRestFill {
    uint64_t due_ns = 0;
    uint64_t order_id = 0;
    int64_t quantity = 0;
    uint16_t symbol_idx = 0;
};

// main_engine is deliberately a simulator.  A REST submit therefore gets a
// deterministic, delayed synthetic venue report.  This queue is exclusively
// owned by the execution thread: HTTP only writes the existing SPSC bridge,
// while WebSocket delivery remains behind ExecutionGateway's fill callback.
// A real venue adapter must not use this type; it applies reports only after
// receiving a venue acknowledgement.
class SimulatedRestFillQueue {
public:
    [[nodiscard]] bool schedule(
        const luv::http::AcceptedSubmission& submission,
        uint64_t now_ns) noexcept {
        if (!submission.accepted || submission.order_id == 0 ||
            submission.quantity <= 0 ||
            submission.symbol_idx >= luv::Config::kSymbols ||
            now_ns > UINT64_MAX - kSimulatedRestFillDelayNs ||
            count_ == entries_.size()) {
            return false;
        }
        const uint64_t earliest_due = now_ns + kSimulatedRestFillDelayNs;
        const uint64_t due_ns = earliest_due > next_due_ns_
            ? earliest_due : next_due_ns_;
        if (due_ns > UINT64_MAX - kSimulatedRestFillSpacingNs) return false;
        entries_[tail_] = {
            .due_ns = due_ns,
            .order_id = submission.order_id,
            .quantity = submission.quantity,
            .symbol_idx = submission.symbol_idx,
        };
        tail_ = (tail_ + 1U) & kMask;
        ++count_;
        next_due_ns_ = due_ns + kSimulatedRestFillSpacingNs;
        return true;
    }

    // Complete at most one FIFO entry per execution-owner turn. This prevents
    // a delayed REST burst from overflowing the bounded WebSocket SPSC queue.
    // Returns false only for an inconsistent live order; a cancelled or
    // already-terminal order is an expected stale simulator event and is
    // discarded without ever calling apply_execution_report.
    [[nodiscard]] bool complete_due(luv::ExecutionGateway& execution,
                                    uint64_t now_ns) noexcept {
        if (count_ == 0 || entries_[head_].due_ns > now_ns) return true;
        const PendingSimulatedRestFill fill = entries_[head_];
        head_ = (head_ + 1U) & kMask;
        --count_;

        luv::ActiveOrder order{};
        if (!execution.query_order(fill.order_id, order) ||
            order.state == 0 || order.state == 3) {
            return true;
        }
        return order.symbol_idx == fill.symbol_idx && order.qty > 0 &&
               execution.apply_execution_report(
                   fill.symbol_idx, {fill.order_id, order.qty, true});
    }

    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

private:
    static constexpr uint32_t kMask = kSimulatedRestFillCapacity - 1U;
    static_assert((kSimulatedRestFillCapacity & kMask) == 0,
                  "simulated fill capacity must be a power of two");
    std::array<PendingSimulatedRestFill, kSimulatedRestFillCapacity> entries_{};
    uint32_t head_ = 0;
    uint32_t tail_ = 0;
    uint32_t count_ = 0;
    uint64_t next_due_ns_ = 0;
};

// This runs only on the execution owner and hands the completed snapshot to
// the metrics worker through Arena's pre-allocated telemetry SPSC ring.  The
// worker never reads execution state directly, so it cannot race the engine.
void publish_metrics_snapshot(luv::Arena& arena,
                              const luv::ExecutionGateway& execution,
                              const uint32_t tick_rate_hz,
                              const float risk_check_latency_ns) noexcept {
    luv::TelemSnapshot snapshot{};
    snapshot.timestamp_ns = utc_now_ns();
    snapshot.tick_rate_hz = tick_rate_hz;
    snapshot.risk_ns = risk_check_latency_ns;
    const uint64_t fills = execution.fill_report_count();
    snapshot.fill_count = static_cast<int32_t>(
        fills > static_cast<uint64_t>(INT32_MAX) ? INT32_MAX : fills);
    snapshot.fills_total = fills;
    snapshot.websocket_fill_notification_drops_total =
        execution.dropped_fill_notifications();

    int64_t rejected = 0;
    for (uint16_t symbol = 0; symbol < luv::Config::kSymbols; ++symbol) {
        luv::RiskState state{};
        if (!execution.query_position(symbol, state)) continue;
        snapshot.session_pnl += state.daily_pnl;
        snapshot.gross_exposure += state.gross_exposure;
        snapshot.active_orders += state.order_count;
        rejected += state.reject_count;
        snapshot.halted = static_cast<uint8_t>(snapshot.halted | state.halted);
    }
    snapshot.reject_count = static_cast<int32_t>(
        rejected > INT32_MAX ? INT32_MAX : rejected);
    snapshot.rejections_total = rejected > 0
        ? static_cast<uint64_t>(rejected) : 0;
    (void)luv::TelemetryPublisher::publish(arena, snapshot);
}

void egress_loop(luv::StaticSpscQueue<luv::OutboundPacket, kPacketQueueCapacity>& queue,
                 const std::atomic<bool>& strategy_done, uint16_t port,
                 std::atomic<uint64_t>& sent,
                 std::atomic<bool>& failed) noexcept {
    int fd = -1;
    sockaddr_in destination{};
    if (port != 0) {
        fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        destination.sin_family = AF_INET;
        destination.sin_port = htons(port);
        (void)::inet_pton(AF_INET, "127.0.0.1", &destination.sin_addr);
    }

    luv::OutboundPacket packet{};
    // Do not exit merely because production stopped: every accepted order is
    // drained before the queue owner tears down.
    while (!strategy_done.load(std::memory_order_acquire) || !queue.empty()) {
        if (!queue.try_pop(packet)) {
            // A long-lived HTTP/metrics control plane can outlast the finite
            // simulator feed.  With UDP egress disabled there is no latency
            // reason for this helper to spin an entire core while idle.
            if (port == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            } else {
                cpu_relax();
            }
            continue;
        }
        if (fd >= 0 && packet.len != 0) {
            const ssize_t result = ::sendto(fd, packet.bytes, packet.len,
                                            MSG_DONTWAIT,
                                            reinterpret_cast<sockaddr*>(&destination),
                                            sizeof(destination));
            if (result == static_cast<ssize_t>(packet.len)) ++sent;
            else failed.store(true, std::memory_order_release);
        }
    }
    if (fd >= 0) ::close(fd);
}

}  // namespace

int main(int argc, char** argv) {
    RunConfig cfg{};
    if (!parse_args(argc, argv, cfg)) {
        std::fprintf(stderr,
                     "Usage: %s [--messages N] [--model PATH] [--udp-port PORT] "
                     "[--http-port PORT] [--prometheus-port PORT "
                     "[--api-token TOKEN | --api-token-file PATH] "
                     "[--metrics-token TOKEN | --metrics-token-file PATH]\n",
                     argv[0]);
        return 2;
    }

    luv::ShutdownController shutdown;

    luv::Arena arena;
    if (!arena.init()) {
        std::fprintf(stderr, "Unable to initialise LUV arena.\n");
        return 1;
    }

    luv::SimConfig feed_cfg{};
    feed_cfg.target_rate_hz = 0;
    feed_cfg.prebuf_count = 1u << 12;
    luv::SimFeedSource feed(feed_cfg);
    if (!feed.init(arena)) return 1;

    luv::Consumer consumer;
    luv::ExecutionGateway execution;
    if (!consumer.init(arena) || !execution.init(arena)) return 1;
    for (uint16_t symbol = 0; symbol < luv::Config::kSymbols; ++symbol) {
        if (!execution.risk().set_limits(
                symbol, {1'000, 100'000, 1'000'000'000})) {
            std::fprintf(stderr, "Invalid risk limits for symbol %u.\n", symbol);
            return 1;
        }
    }

    const bool control_plane_enabled =
        cfg.http_port != 0 || cfg.prometheus_port != 0;
    // One extra byte permits a conventional trailing newline in a mounted
    // Secret. The order-control store enforces its 63-byte API-key limit;
    // the metrics worker supports its separately documented larger limit.
    char file_api_token[luv::http::kMaxBearerTokenBytes + 1U]{};
    char file_metrics_token[luv::MetricsHttpServer::kMaxBearerTokenBytes + 1U]{};
    const char* api_token = cfg.api_token;
    if (cfg.api_token_file) {
        if (!load_api_token_file(cfg.api_token_file, file_api_token,
                                 sizeof(file_api_token))) {
            std::fprintf(stderr, "Unable to load API token file.\n");
            return 1;
        }
        api_token = file_api_token;
    }
    const char* metrics_token = cfg.metrics_token;
    if (cfg.metrics_token_file) {
        if (!load_api_token_file(cfg.metrics_token_file, file_metrics_token,
                                 sizeof(file_metrics_token))) {
            std::fprintf(stderr, "Unable to load metrics token file.\n");
            return 1;
        }
        metrics_token = file_metrics_token;
    }
    // Local development keeps the original one-token CLI. Deployments can
    // supply an independent least-privilege scrape token through either
    // dedicated metrics option.
    if (!metrics_token) metrics_token = api_token;
    luv::http::BearerKeyStore api_keys;
    if (cfg.http_port != 0 && !api_keys.add(api_token)) {
        std::fprintf(stderr, "Invalid API token.\n");
        return 1;
    }

    luv::MetricsHttpServer metrics_server(cfg.prometheus_port);
    luv::TelemSnapshot initial_metrics{};
    initial_metrics.timestamp_ns = utc_now_ns();
    if (cfg.prometheus_port != 0 &&
        !metrics_server.start(arena, initial_metrics, metrics_token)) {
        std::fprintf(stderr, "Unable to start Prometheus metrics server.\n");
        return 1;
    }

    luv::http::ExecutionBridge http_bridge;
    // The 1,000-entry upgrade staging slab is intentionally static rather
    // than consuming the process main-thread stack.
    static luv::ws::Server websocket;
    if (cfg.http_port != 0) {
        if (!websocket.start(arena.websocket_connection_storage)) {
            metrics_server.stop();
            return 1;
        }
        execution.set_fill_event_callback(&luv::ws::Server::publish_callback,
                                          &websocket);
    }
    luv::http::HttpServer http_server(http_bridge, api_keys,
                                      {.bind_address = "0.0.0.0", .port = cfg.http_port}, &websocket);
    if (cfg.http_port != 0 && !http_server.start()) {
        std::fprintf(stderr, "Unable to start HTTP API server.\n");
        execution.set_fill_event_callback(nullptr, nullptr);
        websocket.stop();
        metrics_server.stop();
        return 1;
    }
    // Both in-memory authentication components made fixed-size copies during
    // start-up. Do not retain the mounted Secret's transient read buffer.
    std::memset(file_api_token, 0, sizeof(file_api_token));
    std::memset(file_metrics_token, 0, sizeof(file_metrics_token));

    luv::AIEngine ai;
    if (cfg.model_path) {
        if (!ai.init(arena) || !ai.load_model_file(cfg.model_path)) {
            std::fprintf(stderr, "Unable to load model: %s\n", cfg.model_path);
            http_server.stop();
            execution.set_fill_event_callback(nullptr, nullptr);
            websocket.stop();
            metrics_server.stop();
            return 1;
        }
        consumer.set_ai_engine(&ai);
    }

    std::atomic<bool> ingest_done{false};
    std::atomic<bool> strategy_done{false};
    std::atomic<bool> egress_failed{false};
    std::atomic<uint64_t> sent{0};
    luv::StaticSpscQueue<luv::OutboundPacket, kPacketQueueCapacity> outbound;

    std::thread ingest([&] {
        while (!shutdown.requested() &&
               feed.total_messages() < cfg.message_limit) {
            (void)feed.poll();
        }
        ingest_done.store(true, std::memory_order_release);
    });

    std::thread egress(egress_loop, std::ref(outbound), std::cref(strategy_done),
                       cfg.udp_port, std::ref(sent), std::ref(egress_failed));

    uint64_t accepted = 0;
    uint32_t client_order_id = 1;
    uint64_t last_metrics_publish_ns = monotonic_now_ns();
    uint64_t last_metrics_tick_count = 0;
    float last_execution_latency_ns = 0.0F;
    // Avoid consuming the main-thread stack with the pre-allocated control
    // plane completion slab.
    static SimulatedRestFillQueue simulated_rest_fills;
    while (!shutdown.requested()) {
        if (!simulated_rest_fills.complete_due(execution, monotonic_now_ns())) {
            std::fprintf(stderr, "Unable to apply simulated REST fill.\n");
            luv::ShutdownController::request_shutdown();
            break;
        }
        // Bound control-plane draining so a continuous HTTP producer cannot
        // starve a due simulated venue report or the WebSocket callback path.
        for (uint32_t request_count = 0;
             request_count < kMaxHttpRequestsPerExecutionTurn;
             ++request_count) {
            luv::OutboundPacket http_packet{};
            luv::http::AcceptedSubmission accepted_submission{};
            const uint64_t request_start_ns = monotonic_now_ns();
            if (!http_bridge.process_one(execution, &http_packet,
                                         &accepted_submission)) break;
            last_execution_latency_ns = static_cast<float>(
                monotonic_now_ns() - request_start_ns);

            // Egress is an admission boundary, not a place to spin the
            // single execution owner.  A full fixed SPSC ring rejects this
            // just-built order and releases its reservation immediately.
            if (http_packet.len == 0) {
                if (accepted_submission.accepted &&
                    !execution.apply_reject_report(
                        accepted_submission.symbol_idx,
                        accepted_submission.order_id, 'Q')) {
                    std::fprintf(stderr,
                                 "Unable to reject unencodable REST order %llu.\n",
                                 static_cast<unsigned long long>(
                                     accepted_submission.order_id));
                    luv::ShutdownController::request_shutdown();
                    break;
                }
                continue;
            }
            if (!outbound.try_push(http_packet)) {
                if (accepted_submission.accepted &&
                    !execution.apply_reject_report(
                        accepted_submission.symbol_idx,
                        accepted_submission.order_id, 'Q')) {
                    std::fprintf(stderr,
                                 "Unable to reject egress-blocked REST order %llu.\n",
                                 static_cast<unsigned long long>(
                                     accepted_submission.order_id));
                    luv::ShutdownController::request_shutdown();
                    break;
                }
                continue;
            }
            if (accepted_submission.accepted &&
                !simulated_rest_fills.schedule(
                    accepted_submission, monotonic_now_ns())) {
                std::fprintf(stderr,
                             "Simulated REST fill queue exhausted for order %llu.\n",
                             static_cast<unsigned long long>(
                                 accepted_submission.order_id));
                // The packet has crossed the local egress boundary, but
                // this simulator cannot leave an order reserved without
                // a matching synthetic report.  Reconcile it then halt
                // rather than block or silently leak risk capacity.
                (void)execution.apply_reject_report(
                    accepted_submission.symbol_idx,
                    accepted_submission.order_id, 'Q');
                luv::ShutdownController::request_shutdown();
                break;
            }
        }
        if (shutdown.requested()) break;

        const uint64_t now_ns = monotonic_now_ns();
        if (cfg.prometheus_port != 0 &&
            now_ns - last_metrics_publish_ns >= 100'000'000ULL) {
            const uint64_t processed = consumer.ticks_processed();
            const uint64_t elapsed_ns = now_ns - last_metrics_publish_ns;
            const uint64_t tick_delta = processed - last_metrics_tick_count;
            const uint64_t rate = elapsed_ns == 0 ? 0 :
                (tick_delta * 1'000'000'000ULL) / elapsed_ns;
            publish_metrics_snapshot(
                arena, execution,
                static_cast<uint32_t>(rate > UINT32_MAX ? UINT32_MAX : rate),
                last_execution_latency_ns);
            last_metrics_publish_ns = now_ns;
            last_metrics_tick_count = processed;
        }

        luv::TickMsg* tick = arena.tick_ring.try_peek();
        if (!tick) {
            const bool simulator_finished =
                ingest_done.load(std::memory_order_acquire) &&
                arena.tick_ring.size() == 0;
            if (simulator_finished) {
                if (!control_plane_enabled) break;
                // The simulator completion FIFO is checked every owner turn.
                // Do not sleep across its deterministic deadline; idle runs
                // without pending synthetic reports still yield the core.
                if (!simulated_rest_fills.empty()) {
                    cpu_relax();
                } else {
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                }
            } else {
                cpu_relax();
            }
            continue;
        }
        const uint16_t symbol = tick->symbol_idx;
        const int64_t price = tick->price;
        (void)consumer.process_one();

        if (!cfg.model_path) continue;
        const luv::SignalOutput& signal = arena.signal(symbol);
        if (signal.direction == 0 || price <= 0) continue;
        luv::exec::OrderIntent intent{};
        intent.symbol_idx = symbol;
        intent.side = signal.direction > 0 ? luv::exec::kBuy : luv::exec::kSell;
        intent.qty = 1;
        intent.price = price;
        // Feed timestamps and host clocks are different domains.  Timestamp
        // alpha at evaluation so the stale-alpha guard measures real elapsed
        // strategy time rather than comparing an exchange value to itself.
        intent.alpha_timestamp_ns = monotonic_now_ns();
        intent.now_ns = monotonic_now_ns();
        intent.client_order_id = client_order_id++;
        luv::OutboundPacket packet{};
        const uint64_t risk_start_ns = monotonic_now_ns();
        if (execution.try_build(intent, packet).pass) {
            if (outbound.try_push(packet)) {
                ++accepted;
            } else if (!execution.apply_reject_report(
                           symbol, intent.client_order_id, 'Q')) {
                std::fprintf(stderr,
                             "Unable to reject egress-blocked strategy order %u.\n",
                             intent.client_order_id);
                luv::ShutdownController::request_shutdown();
                break;
            }
        }
        last_execution_latency_ns = static_cast<float>(
            monotonic_now_ns() - risk_start_ns);
    }
    strategy_done.store(true, std::memory_order_release);
    ingest.join();
    egress.join();
    http_server.stop();
    // The execution thread owns callback invocation.  Remove the pointer
    // before its WebSocket target is stopped or unmapped.
    execution.set_fill_event_callback(nullptr, nullptr);
    websocket.stop();
    metrics_server.stop();

    std::printf("processed=%llu accepted=%llu udp_sent=%llu\n",
                static_cast<unsigned long long>(consumer.ticks_processed()),
                static_cast<unsigned long long>(accepted),
                static_cast<unsigned long long>(sent.load()));
    return egress_failed.load(std::memory_order_acquire) ? 1 : 0;
}
