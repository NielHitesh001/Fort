#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "luv_arena.hpp"
#include "luv_execution.hpp"
#include "luv_http_server.hpp"
#include "luv_websocket.hpp"

namespace {
int connect_loopback(uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0); assert(fd >= 0);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port);
    assert(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    assert(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    return fd;
}
std::string request(uint16_t port, const char* method, const char* path, const char* token, const char* body = "") {
    const int fd = connect_loopback(port);
    char payload[1024]{};
    const int n = std::snprintf(payload, sizeof(payload), "%s %s HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s", method, path, token, std::strlen(body), body);
    assert(n > 0 && size_t(n) < sizeof(payload)); assert(::send(fd, payload, size_t(n), 0) == n);
    std::string response; char buffer[512]; ssize_t got = 0; while ((got = ::recv(fd, buffer, sizeof(buffer), 0)) > 0) response.append(buffer, size_t(got)); ::close(fd); return response;
}
std::string websocket_upgrade(uint16_t port, uint64_t order_id) {
    const int fd = connect_loopback(port);
    char payload[512]{};
    const int sent = std::snprintf(
        payload, sizeof(payload),
        "GET /api/v1/stream/%llu HTTP/1.1\r\nHost: localhost\r\n"
        "Authorization: Bearer test-token\r\nUpgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n",
        static_cast<unsigned long long>(order_id));
    assert(sent > 0 && static_cast<size_t>(sent) < sizeof(payload));
    assert(::send(fd, payload, static_cast<size_t>(sent), 0) == sent);

    std::string response;
    char buffer[512]{};
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < deadline &&
           response.find("\r\n\r\n") == std::string::npos) {
        pollfd poll_fd{.fd = fd, .events = POLLIN, .revents = 0};
        const int ready = ::poll(&poll_fd, 1, 20);
        if (ready <= 0) continue;
        const ssize_t received = ::recv(fd, buffer, sizeof(buffer), 0);
        if (received <= 0) break;
        response.append(buffer, static_cast<size_t>(received));
    }
    ::close(fd);
    return response;
}
size_t http_content_length(const std::string& response) {
    const size_t header = response.find("Content-Length: ");
    assert(header != std::string::npos);
    size_t cursor = header + std::strlen("Content-Length: ");
    size_t value = 0;
    assert(cursor < response.size());
    while (cursor < response.size() && response[cursor] >= '0' &&
           response[cursor] <= '9') {
        value = value * 10U +
            static_cast<size_t>(response[cursor] - '0');
        ++cursor;
    }
    return value;
}

void append_socket_bytes(const int fd, std::string& response,
                         const size_t chunk_bytes) {
    char chunk[64]{};
    assert(chunk_bytes > 0 && chunk_bytes <= sizeof(chunk));
    const ssize_t received = ::recv(fd, chunk, chunk_bytes, 0);
    assert(received > 0);
    response.append(chunk, static_cast<size_t>(received));
}

template <typename Pump> std::string request_and_pump(Pump&& pump, uint16_t port,
                                                       const char* method,
                                                       const char* path) {
    // The HTTP server waits for an execution-owner response.  Keep pumping
    // until the client has received its reply; reading `response` before the
    // client thread finishes would be a data race and a fixed yield count can
    // expire before the request reaches the server on a contended host.
    std::string response;
    std::atomic<bool> client_done{false};
    std::thread client([&] {
        response = request(port, method, path, "test-token");
        client_done.store(true, std::memory_order_release);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!client_done.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        pump();
        std::this_thread::yield();
    }
    client.join();
    return response;
}
}  // namespace

int main() {
    luv::Arena arena; assert(arena.init()); luv::ExecutionGateway gateway; assert(gateway.init(arena));
    luv::exec::RiskLimits limits{}; limits.max_order_qty = 2'000; limits.max_abs_position = 20'000; limits.max_alpha_age_ns = 1'000'000'000ULL;
    for (uint16_t symbol = 0; symbol < luv::Config::kSymbols; ++symbol)
        assert(gateway.risk().set_limits(symbol, limits));
    luv::http::BearerKeyStore keys; assert(keys.add("test-token")); luv::http::ExecutionBridge bridge;
    luv::ws::Server websocket(16);
    assert(websocket.start(arena.websocket_connection_storage));
    luv::http::HttpServer server(bridge, keys,
        {.bind_address = "127.0.0.1", .port = 0, .response_wait_ms = 500,
         .max_write_chunk_bytes = 64},
        &websocket); assert(server.start());
    const std::string unauthorized = request(server.port(), "GET", "/api/v1/positions", "bad");
    assert(unauthorized.find("HTTP/1.1 401 Unauthorized") == 0);
    const char* body = "{\"symbol_idx\":2,\"side\":\"buy\",\"qty\":100,\"price\":1000000}";
    const std::string submitted = request(server.port(), "POST", "/api/v1/orders", "test-token", body);
    assert(submitted.find("HTTP/1.1 202 Accepted") == 0 && submitted.find("\"order_id\":1") != std::string::npos);
    luv::http::AcceptedSubmission first_submission{};
    assert(bridge.process_one(gateway, nullptr, &first_submission));
    assert(first_submission.accepted && first_submission.order_id == 1 &&
           first_submission.symbol_idx == 2 && first_submission.quantity == 100);
    // The REST-facing ticker form uses the documented case-insensitive
    // FNV-1a mapping.  AAPL hashes to index 267 for the 512-symbol simulator.
    const char* live_body = "{\"symbol\":\"AAPL\",\"side\":\"buy\",\"qty\":100,\"price\":150.00}";
    const std::string live_submitted = request(server.port(), "POST", "/api/v1/orders", "test-token", live_body);
    assert(live_submitted.find("HTTP/1.1 202 Accepted") == 0 && live_submitted.find("\"order_id\":2") != std::string::npos);
    luv::http::AcceptedSubmission live_submission{};
    assert(bridge.process_one(gateway, nullptr, &live_submission));
    assert(live_submission.accepted && live_submission.order_id == 2 &&
           live_submission.symbol_idx == 267 && live_submission.quantity == 100);
    luv::ActiveOrder live_order{};
    assert(gateway.query_order(2, live_order));
    assert(live_order.symbol_idx == 267 && live_order.side == luv::exec::kBuy);
    assert(live_order.qty == 100 && live_order.price == 1'500'000);
    const std::string queried = request_and_pump([&] { (void)bridge.process_one(gateway); }, server.port(), "GET", "/api/v1/orders/1");
    assert(queried.find("HTTP/1.1 200 OK") == 0 && queried.find("\"status\":\"live\"") != std::string::npos);
    const std::string positions = request_and_pump([&] { (void)bridge.process_one(gateway); }, server.port(), "GET", "/api/v1/positions");
    assert(positions.find("{\"symbol_idx\":2,") != std::string::npos && positions.find("\"net_position\":100") != std::string::npos);
    const std::string cancelled = request_and_pump([&] { (void)bridge.process_one(gateway); }, server.port(), "DELETE", "/api/v1/orders/1");
    assert(cancelled.find("HTTP/1.1 200 OK") == 0 && cancelled.find("\"cancelled\"") != std::string::npos);
    const std::string positions_after_cancel = request_and_pump(
        [&] { (void)bridge.process_one(gateway); }, server.port(),
        "GET", "/api/v1/positions");
    // The AAPL order is still live, but cancelling order 1 must release that
    // order's distinct symbol-2 reservation from position and exposure.
    assert(positions_after_cancel.find("{\"symbol_idx\":2,") == std::string::npos);
    assert(positions_after_cancel.find("{\"symbol_idx\":267,") != std::string::npos);
    const std::string malformed_price = request(server.port(), "POST", "/api/v1/orders", "test-token", "{\"symbol\":\"AAPL\",\"side\":\"buy\",\"qty\":100,\"price\":150.00001}");
    assert(malformed_price.find("HTTP/1.1 400 Bad Request") == 0);

    // A DELETE racing a terminal execution must be a harmless 404 rather
    // than a reconciliation failure that trips the execution breaker.
    const std::string terminal_submitted = request(
        server.port(), "POST", "/api/v1/orders", "test-token", body);
    assert(terminal_submitted.find("HTTP/1.1 202 Accepted") == 0 &&
           terminal_submitted.find("\"order_id\":3") != std::string::npos);
    luv::http::AcceptedSubmission terminal_submission{};
    assert(bridge.process_one(gateway, nullptr, &terminal_submission));
    assert(terminal_submission.accepted && terminal_submission.order_id == 3);
    assert(gateway.apply_execution_report(
        terminal_submission.symbol_idx,
        {terminal_submission.order_id, terminal_submission.quantity, true}));
    const std::string terminal_cancelled = request_and_pump(
        [&] { (void)bridge.process_one(gateway); }, server.port(), "DELETE",
        "/api/v1/orders/3");
    assert(terminal_cancelled.find("HTTP/1.1 404 Not Found") == 0);
    assert(!gateway.circuit_breaker().tripped());

    // The preallocated response body must contain every configured symbol;
    // truncating a valid-looking 200 would conceal live exposure on later
    // symbols.
    for (uint16_t symbol = 0; symbol < luv::Config::kSymbols; ++symbol) {
        arena.exec_states[symbol].risk.net_position =
            static_cast<int64_t>(symbol) + 1;
        arena.exec_states[symbol].risk.gross_exposure =
            (static_cast<int64_t>(symbol) + 1) * 1'000;
    }
    const std::string all_positions = request_and_pump(
        [&] { (void)bridge.process_one(gateway); }, server.port(), "GET",
        "/api/v1/positions");
    assert(all_positions.find("HTTP/1.1 200 OK") == 0);
    assert(all_positions.find(
        "{\"symbol_idx\":0,\"net_position\":1,\"gross_exposure\":1000}") !=
        std::string::npos);
    assert(all_positions.find(
        "{\"symbol_idx\":511,\"net_position\":512,\"gross_exposure\":512000}") !=
        std::string::npos);

    // Force one bounded nonblocking write per event-loop turn.  A raw client
    // first observes the /positions response but deliberately does not drain
    // it while another REST client submits an order.  The listener must keep
    // accepting the REST work, then resume the original header/body offsets
    // until its exact Content-Length is delivered and the connection closes.
    const int buffered_positions = connect_loopback(server.port());
    int receive_buffer = 1024;
    (void)::setsockopt(buffered_positions, SOL_SOCKET, SO_RCVBUF,
                       &receive_buffer, sizeof(receive_buffer));
    constexpr char positions_request[] =
        "GET /api/v1/positions HTTP/1.1\r\nHost: localhost\r\n"
        "Authorization: Bearer test-token\r\nConnection: close\r\n\r\n";
    assert(::send(buffered_positions, positions_request,
                  sizeof(positions_request) - 1U, 0) ==
           static_cast<ssize_t>(sizeof(positions_request) - 1U));
    bool processed_positions = false;
    const auto process_deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(1);
    while (!processed_positions &&
           std::chrono::steady_clock::now() < process_deadline) {
        processed_positions = bridge.process_one(gateway);
        if (!processed_positions) std::this_thread::yield();
    }
    assert(processed_positions);
    pollfd first_byte{.fd = buffered_positions, .events = POLLIN, .revents = 0};
    assert(::poll(&first_byte, 1, 1'000) > 0);
    std::string buffered_response;
    append_socket_bytes(buffered_positions, buffered_response, 1);

    const auto concurrent_rest_start = std::chrono::steady_clock::now();
    const std::string concurrent_rest = request(
        server.port(), "POST", "/api/v1/orders", "test-token", body);
    const auto concurrent_rest_elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - concurrent_rest_start);
    assert(concurrent_rest.find("HTTP/1.1 202 Accepted") == 0);
    assert(concurrent_rest_elapsed.count() < 200);
    assert(bridge.process_one(gateway));

    size_t expected_response_size = 0;
    const auto receive_deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < receive_deadline) {
        const size_t header_end = buffered_response.find("\r\n\r\n");
        if (header_end != std::string::npos && expected_response_size == 0) {
            expected_response_size = header_end + 4U +
                http_content_length(buffered_response);
        }
        if (expected_response_size != 0 &&
            buffered_response.size() >= expected_response_size) {
            break;
        }
        pollfd readable{.fd = buffered_positions, .events = POLLIN, .revents = 0};
        assert(::poll(&readable, 1, 100) > 0);
        append_socket_bytes(buffered_positions, buffered_response, 31);
    }
    assert(expected_response_size != 0);
    assert(buffered_response.size() == expected_response_size);
    assert(buffered_response.find("HTTP/1.1 200 OK") == 0);
    assert(buffered_response.find(
        "{\"symbol_idx\":511,\"net_position\":512,\"gross_exposure\":512000}") !=
        std::string::npos);
    const auto close_deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(1);
    bool closed = false;
    while (!closed && std::chrono::steady_clock::now() < close_deadline) {
        pollfd closing{.fd = buffered_positions, .events = POLLIN, .revents = 0};
        if (::poll(&closing, 1, 20) <= 0) continue;
        char byte = 0;
        const ssize_t received = ::recv(buffered_positions, &byte, 1, 0);
        closed = received == 0;
    }
    assert(closed);
    ::close(buffered_positions);

    // A partial unauthenticated header occupies only one fixed slot. The
    // valid REST submit and WebSocket upgrade must both complete well before
    // that slot's 500 ms deadline instead of waiting behind a slowloris.
    const int slow_client = connect_loopback(server.port());
    constexpr char slow_header[] =
        "GET /api/v1/positions HTTP/1.1\r\nHost: localhost\r\n";
    assert(::send(slow_client, slow_header, sizeof(slow_header) - 1U, 0) ==
           static_cast<ssize_t>(sizeof(slow_header) - 1U));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto rest_start = std::chrono::steady_clock::now();
    const std::string timely_rest = request(
        server.port(), "POST", "/api/v1/orders", "test-token", body);
    const auto rest_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - rest_start);
    assert(timely_rest.find("HTTP/1.1 202 Accepted") == 0);
    assert(rest_elapsed.count() < 200);
    assert(bridge.process_one(gateway));

    const auto ws_start = std::chrono::steady_clock::now();
    const std::string timely_ws = websocket_upgrade(server.port(), 4);
    const auto ws_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - ws_start);
    assert(timely_ws.find("HTTP/1.1 101 Switching Protocols") == 0);
    assert(ws_elapsed.count() < 200);
    ::close(slow_client);
    server.stop();
    websocket.stop();

    // A 32-bit OUCH token cannot safely wrap.  The last usable value is
    // admitted once; the next REST order must fail closed rather than reuse
    // order ID 1 and alias query/cancel/WebSocket subscription state.
    luv::http::HttpServer exhausted_ids(
        bridge, keys,
        {.bind_address = "127.0.0.1", .port = 0,
         .first_order_id = UINT32_MAX});
    assert(exhausted_ids.start());
    const std::string final_order = request(
        exhausted_ids.port(), "POST", "/api/v1/orders", "test-token", body);
    assert(final_order.find("HTTP/1.1 202 Accepted") == 0 &&
           final_order.find("\"order_id\":4294967295") != std::string::npos);
    assert(bridge.process_one(gateway));
    const std::string exhausted_order = request(
        exhausted_ids.port(), "POST", "/api/v1/orders", "test-token", body);
    assert(exhausted_order.find("HTTP/1.1 503 Service Unavailable") == 0 &&
           exhausted_order.find("\"order_id_exhausted\"") != std::string::npos);
    exhausted_ids.stop();

    // The execution owner must not silently lose the ninth snapshot when the
    // fixed eight-slot response ring is full. The deferred fixed slot is
    // retried before another request is consumed, preserving all request IDs
    // without heap-backed retry state.
    constexpr uint64_t first_deferred_request = 70'000;
    for (uint64_t offset = 0; offset < 9; ++offset) {
        luv::http::Request query{};
        query.command = luv::http::Command::kQuery;
        query.request_id = first_deferred_request + offset;
        query.order_id = 2;
        assert(bridge.submit(query));
    }
    for (uint32_t index = 0; index < 9; ++index) {
        assert(bridge.process_one(gateway));
    }
    std::array<bool, 9> delivered{};
    for (uint32_t index = 0; index < 8; ++index) {
        luv::http::Response response{};
        assert(bridge.try_take_response(response));
        assert(response.request_id >= first_deferred_request &&
               response.request_id < first_deferred_request + delivered.size());
        delivered[response.request_id - first_deferred_request] = true;
    }
    // No new request remains, but process_one() reports the deferred reply it
    // successfully admitted to the response ring.
    assert(bridge.process_one(gateway));
    luv::http::Response final_deferred_response{};
    assert(bridge.try_take_response(final_deferred_response));
    delivered[final_deferred_response.request_id - first_deferred_request] = true;
    for (const bool received : delivered) assert(received);

    std::vector<long long> samples; samples.reserve(1000);
    for (uint32_t i = 0; i < 1000; ++i) {
        luv::http::Request command{}; command.command = luv::http::Command::kSubmit; command.request_id = 10'000 + i;
        command.order = {.symbol_idx = 2, .side = luv::exec::kBuy, .qty = 1, .price = 1'000'000, .alpha_timestamp_ns = 1, .now_ns = 1, .client_order_id = 10'000 + i};
        const auto start = std::chrono::steady_clock::now(); assert(bridge.submit(command)); assert(bridge.process_one(gateway));
        samples.push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count()); luv::http::Response response{}; (void)bridge.try_take_response(response);
    }
    std::sort(samples.begin(), samples.end()); assert(samples[990] < 500); std::printf("HTTP execution enqueue p99=%lld us\n", samples[990]); std::puts("HTTP API server tests passed");
}
