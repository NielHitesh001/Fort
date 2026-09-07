#include <cassert>
#include <cstdio>
#include <string>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
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

void test_metrics_http_server() {
    luv::TelemSnapshot snapshot{};
    snapshot.session_pnl = 12345;
    snapshot.tick_rate_hz = 42000;

    luv::MetricsHttpServer server(0);
    assert(server.start(snapshot));
    assert(server.port() != 0);

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(server.port());
    assert(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    assert(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);

    constexpr const char request[] =
        "GET /metrics HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    assert(::send(fd, request, sizeof(request) - 1, 0) ==
           static_cast<ssize_t>(sizeof(request) - 1));

    std::string response;
    char buffer[512];
    ssize_t received = 0;
    while ((received = ::recv(fd, buffer, sizeof(buffer), 0)) > 0)
        response.append(buffer, static_cast<size_t>(received));
    ::close(fd);
    server.stop();

    assert(response.find("HTTP/1.1 200 OK") == 0);
    assert(response.find("Content-Type: text/plain; version=0.0.4") != std::string::npos);
    assert(response.find("# TYPE luv_session_pnl gauge") != std::string::npos);
    assert(response.find("luv_session_pnl 12345") != std::string::npos);
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
    luv::Arena arena;
    assert(arena.init());

    luv::TelemSnapshot snap{};
    snap.session_pnl = 12345;
    snap.gross_exposure = 98765;
    snap.fill_count = 12;
    snap.reject_count = 3;
    snap.tick_rate_hz = 42000;
    snap.active_orders = 7;
    snap.inference_us = 3.5f;
    snap.risk_ns = 99.0f;
    snap.halted = 0;

    const std::string metrics = luv::PrometheusMetricsExporter::render(snap);
    assert(metrics.find("# HELP luv_session_pnl") != std::string::npos);
    assert(metrics.find("luv_session_pnl") != std::string::npos);
    assert(metrics.find("luv_fill_count") != std::string::npos);
    assert(metrics.find("luv_active_orders") != std::string::npos);
    assert(metrics.find("luv_risk_ns") != std::string::npos);
    assert(metrics.find("12345") != std::string::npos);
    assert(metrics.find("42000") != std::string::npos);

    std::printf("[OK] Prometheus export format\n");
    test_metrics_http_server();
    std::printf("[OK] HTTP /metrics endpoint\n");
    test_health_check_harness();
    std::printf("[OK] health checks\n");
    return 0;
}
