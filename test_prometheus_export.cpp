#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "luv_arena.hpp"
#include "luv_telemetry.hpp"

namespace {

uint64_t now_ns() {
    timespec ts {};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

std::string request(uint16_t port, const char* text) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    timeval timeout{};
    timeout.tv_sec = 2;
    assert(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    assert(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    assert(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);

    const size_t bytes = std::strlen(text);
    assert(::send(fd, text, bytes, 0) == static_cast<ssize_t>(bytes));

    std::string response;
    char buffer[1024];
    ssize_t received = 0;
    while ((received = ::recv(fd, buffer, sizeof(buffer), 0)) > 0) {
        response.append(buffer, static_cast<size_t>(received));
    }
    ::close(fd);
    return response;
}

int connect_client(uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    assert(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    assert(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    return fd;
}

void test_metrics_http_server() {
    luv::Arena arena;
    assert(arena.init());

    luv::TelemSnapshot initial{};
    initial.session_pnl = 12345;
    initial.tick_rate_hz = 42000;
    initial.inference_us = 3.5f;
    initial.risk_ns = 99.0f;

    luv::MetricsHttpServer server(0);
    assert(server.start(arena, initial, "metrics-test-token"));
    assert(server.port() != 0);
    assert(server.bound_address() == htonl(INADDR_ANY));

    constexpr char unauthenticated[] =
        "GET /metrics HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    const std::string missing_auth = request(server.port(), unauthenticated);
    assert(missing_auth.starts_with("HTTP/1.1 401 Unauthorized"));
    assert(missing_auth.find("WWW-Authenticate: Bearer realm=\"metrics\"") !=
           std::string::npos);

    constexpr char bad_auth[] =
        "GET /metrics HTTP/1.1\r\nHost: localhost\r\n"
        "Authorization: Bearer wrong-token\r\nConnection: close\r\n\r\n";
    const std::string rejected = request(server.port(), bad_auth);
    assert(rejected.starts_with("HTTP/1.1 401 Unauthorized"));

    constexpr char health[] =
        "GET /healthz HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    const std::string healthy = request(server.port(), health);
    assert(healthy.starts_with("HTTP/1.1 200 OK"));
    assert(healthy.ends_with("ok\n"));

    // An incomplete scrape cannot hold the metrics worker indefinitely.  The
    // worker uses one elapsed-time deadline rather than extending it on each
    // would-block event, so a subsequent probe still completes promptly.
    const int slow_client = connect_client(server.port());
    const auto probe_start = std::chrono::steady_clock::now();
    const std::string probe_after_slow_client = request(server.port(), health);
    const auto probe_elapsed = std::chrono::steady_clock::now() - probe_start;
    assert(probe_after_slow_client.starts_with("HTTP/1.1 200 OK"));
    assert(probe_elapsed < std::chrono::milliseconds(500));
    ::close(slow_client);

    // The producer remains allocation- and lock-free: it writes the arena
    // SPSC ring while the metrics worker is its only consumer.
    luv::TelemSnapshot updated{};
    updated.session_pnl = 777;
    updated.gross_exposure = 999;
    updated.fill_count = 12;
    updated.reject_count = 3;
    updated.fills_total = 12;
    updated.rejections_total = 3;
    updated.websocket_fill_notification_drops_total = 2;
    updated.tick_rate_hz = 123;
    updated.active_orders = 7;
    updated.inference_us = 6.25f;
    updated.risk_ns = 42.0f;
    updated.halted = 1;
    assert(luv::TelemetryPublisher::publish(arena, updated));
    arena.telemetry_drops.store(4, std::memory_order_relaxed);

    constexpr char valid[] =
        "GET /metrics HTTP/1.1\r\nHost: localhost\r\n"
        "authorization: bearer metrics-test-token\r\nConnection: close\r\n\r\n";
    const std::string metrics = request(server.port(), valid);
    assert(metrics.starts_with("HTTP/1.1 200 OK"));
    assert(metrics.find("Content-Type: text/plain; version=0.0.4") != std::string::npos);
    assert(metrics.find("# TYPE luv_execution_session_pnl gauge") != std::string::npos);
    assert(metrics.find("luv_execution_session_pnl 777") != std::string::npos);
    assert(metrics.find("luv_execution_gross_exposure 999") != std::string::npos);
    assert(metrics.find("luv_execution_fills_observed 12") != std::string::npos);
    assert(metrics.find("luv_execution_rejections_observed 3") != std::string::npos);
    assert(metrics.find("# TYPE luv_execution_fills_total counter") != std::string::npos);
    assert(metrics.find("luv_execution_fills_total 12") != std::string::npos);
    assert(metrics.find("# TYPE luv_execution_rejections_total counter") != std::string::npos);
    assert(metrics.find("luv_execution_rejections_total 3") != std::string::npos);
    assert(metrics.find("# TYPE luv_websocket_fill_notification_drops_total counter") !=
           std::string::npos);
    assert(metrics.find("luv_websocket_fill_notification_drops_total 2") !=
           std::string::npos);
    assert(metrics.find("luv_execution_inference_latency_microseconds 6.250") !=
           std::string::npos);
    assert(metrics.find("luv_execution_risk_check_latency_nanoseconds 42.000") !=
           std::string::npos);
    assert(metrics.find("luv_telemetry_queue_depth") != std::string::npos);
    assert(metrics.find("luv_telemetry_dropped_snapshots_total 4") != std::string::npos);

    server.stop();
}

void test_fixed_buffer_exporter() {
    luv::TelemSnapshot snap{};
    snap.session_pnl = 12345;
    snap.gross_exposure = 98765;
    snap.fill_count = 12;
    snap.reject_count = 3;
    snap.fills_total = 12;
    snap.rejections_total = 3;
    snap.websocket_fill_notification_drops_total = 2;
    snap.tick_rate_hz = 42000;
    snap.active_orders = 7;
    snap.inference_us = 3.5f;
    snap.risk_ns = 99.0f;

    char buffer[luv::PrometheusMetricsExporter::kMaxRenderedBytes]{};
    const size_t bytes = luv::PrometheusMetricsExporter::render_into(
        buffer, sizeof(buffer), snap, 9, 2);
    assert(bytes != 0);
    const std::string metrics(buffer, bytes);  // Test-only allocation.
    assert(metrics.find("# HELP luv_execution_session_pnl") != std::string::npos);
    assert(metrics.find("luv_execution_fills_observed") != std::string::npos);
    assert(metrics.find("luv_execution_fills_total 12") != std::string::npos);
    assert(metrics.find("luv_execution_rejections_total 3") != std::string::npos);
    assert(metrics.find("luv_websocket_fill_notification_drops_total 2") !=
           std::string::npos);
    assert(metrics.find("luv_execution_active_orders") != std::string::npos);
    assert(metrics.find("luv_execution_risk_check_latency_nanoseconds") !=
           std::string::npos);
    assert(metrics.find("# TYPE luv_session_pnl gauge") != std::string::npos);
    assert(metrics.find("luv_session_pnl 12345") != std::string::npos);
    assert(metrics.find("# TYPE luv_fill_count gauge") != std::string::npos);
    assert(metrics.find("# TYPE luv_reject_count gauge") != std::string::npos);
    assert(metrics.find("luv_telemetry_queue_depth 9") != std::string::npos);
    assert(metrics.find("luv_telemetry_dropped_snapshots_total 2") != std::string::npos);
    assert(metrics.find("12345") != std::string::npos);
    assert(metrics.find("42000") != std::string::npos);
}

void test_health_check_harness() {
    luv::TelemSnapshot snapshot{};
    snapshot.active_orders = 12;
    snapshot.halted = 0;

    const uint64_t now = now_ns();
    const auto healthy = luv::run_health_checks(
        snapshot,
        now - 20'000'000ULL,
        25ULL,
        1'500'000'000ULL,
        1'500'000'000ULL,
        35.0,
        1000U,
        2'000'000'000ULL,
        1'000'000'000ULL,
        80.0);
    assert(healthy.feed_fresh);
    assert(healthy.lob_consistent);
    assert(healthy.execution_backlog);
    assert(healthy.memory_ok);
    assert(healthy.disk_space_ok);
    assert(healthy.cpu_usage_ok);

    const auto stale = luv::run_health_checks(
        snapshot,
        now - 250'000'000ULL,
        1250ULL,
        1'500'000'000ULL,
        1'500'000'000ULL,
        90.0,
        1000U,
        2'000'000'000ULL,
        1'000'000'000ULL,
        80.0);
    assert(!stale.feed_fresh);
    assert(!stale.execution_backlog);
    assert(!stale.cpu_usage_ok);
}

}  // namespace

int main() {
    test_fixed_buffer_exporter();
    std::printf("[OK] fixed-buffer Prometheus exposition\n");
    test_metrics_http_server();
    std::printf("[OK] authenticated wildcard HTTP /metrics endpoint\n");
    test_health_check_harness();
    std::printf("[OK] health checks\n");
    return 0;
}
