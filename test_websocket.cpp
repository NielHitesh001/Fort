#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "luv_arena.hpp"
#include "luv_execution.hpp"
#include "luv_http_server.hpp"
#include "luv_websocket.hpp"

namespace {

constexpr char kToken[] = "test-token";
constexpr char kWebSocketKey[] = "dGhlIHNhbXBsZSBub25jZQ==";
constexpr char kWebSocketAccept[] = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
constexpr uint64_t kSecondNs = 1'000'000'000ULL;
constexpr uint64_t kShortTimeoutNs = 2 * kSecondNs;
constexpr uint64_t kLongTimeoutNs = 10 * kSecondNs;

[[nodiscard]] uint64_t monotonic_ns() noexcept {
    timespec ts{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * kSecondNs +
           static_cast<uint64_t>(ts.tv_nsec);
}

[[nodiscard]] uint64_t utc_ns() noexcept {
    timespec ts{};
    (void)::clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * kSecondNs +
           static_cast<uint64_t>(ts.tv_nsec);
}

template <typename Predicate>
[[nodiscard]] bool wait_until(Predicate&& predicate,
                              uint64_t timeout_ns = kShortTimeoutNs) {
    const uint64_t deadline = monotonic_ns() + timeout_ns;
    do {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    } while (monotonic_ns() < deadline);
    return predicate();
}

void close_fd(int& fd) noexcept {
    if (fd < 0) return;
    (void)::shutdown(fd, SHUT_RDWR);
    (void)::close(fd);
    fd = -1;
}

[[nodiscard]] bool send_all(int fd, const void* source, size_t count) noexcept {
    const auto* bytes = static_cast<const uint8_t*>(source);
    while (count != 0) {
        const ssize_t sent = ::send(fd, bytes, count, MSG_NOSIGNAL);
        if (sent > 0) {
            bytes += sent;
            count -= static_cast<size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

[[nodiscard]] bool wait_readable(int fd, uint64_t deadline_ns) noexcept {
    while (monotonic_ns() < deadline_ns) {
        const uint64_t remaining_ns = deadline_ns - monotonic_ns();
        const int timeout_ms = static_cast<int>(std::min<uint64_t>(
            100ULL, std::max<uint64_t>(1ULL, (remaining_ns + 999'999ULL) / 1'000'000ULL)));
        pollfd poll_fd{.fd = fd, .events = POLLIN, .revents = 0};
        const int result = ::poll(&poll_fd, 1, timeout_ms);
        if (result > 0) return (poll_fd.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
        if (result < 0 && errno != EINTR) return false;
    }
    return false;
}

[[nodiscard]] bool read_exact(int fd, void* destination, size_t count,
                              uint64_t timeout_ns = kShortTimeoutNs) noexcept {
    auto* bytes = static_cast<uint8_t*>(destination);
    const uint64_t deadline = monotonic_ns() + timeout_ns;
    while (count != 0) {
        if (!wait_readable(fd, deadline)) return false;
        const ssize_t received = ::recv(fd, bytes, count, 0);
        if (received > 0) {
            bytes += received;
            count -= static_cast<size_t>(received);
            continue;
        }
        if (received < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

struct HttpReply {
    std::array<char, 8192> bytes{};
    size_t size = 0;

    [[nodiscard]] bool contains(const char* text) const noexcept {
        return text != nullptr && std::strstr(bytes.data(), text) != nullptr;
    }

    [[nodiscard]] bool starts_with(const char* text) const noexcept {
        return text != nullptr &&
               std::strncmp(bytes.data(), text, std::strlen(text)) == 0;
    }
};

[[nodiscard]] bool has_http_headers(const HttpReply& reply) noexcept {
    return std::strstr(reply.bytes.data(), "\r\n\r\n") != nullptr;
}

[[nodiscard]] bool read_http_reply(int fd, HttpReply& reply, bool until_eof,
                                   uint64_t timeout_ns = kShortTimeoutNs) noexcept {
    const uint64_t deadline = monotonic_ns() + timeout_ns;
    while (monotonic_ns() < deadline && reply.size + 1 < reply.bytes.size()) {
        if (!until_eof && has_http_headers(reply)) return true;
        if (!wait_readable(fd, deadline)) return !until_eof && has_http_headers(reply);
        const ssize_t received = ::recv(fd, reply.bytes.data() + reply.size,
                                        reply.bytes.size() - reply.size - 1, 0);
        if (received > 0) {
            reply.size += static_cast<size_t>(received);
            reply.bytes[reply.size] = '\0';
            continue;
        }
        if (received == 0) return has_http_headers(reply);
        if (errno != EINTR) return false;
    }
    return (!until_eof && has_http_headers(reply)) ||
           (until_eof && has_http_headers(reply));
}

[[nodiscard]] int connect_loopback(uint16_t port) noexcept {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1 ||
        ::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        (void)::close(fd);
        return -1;
    }
    return fd;
}

[[nodiscard]] int connect_loopback_retry(uint16_t port,
                                          uint64_t timeout_ns = kShortTimeoutNs) noexcept {
    const uint64_t deadline = monotonic_ns() + timeout_ns;
    do {
        const int fd = connect_loopback(port);
        if (fd >= 0) return fd;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    } while (monotonic_ns() < deadline);
    return -1;
}

[[nodiscard]] bool send_upgrade_request(int fd, uint64_t order_id,
                                        const char* key) noexcept {
    std::array<char, 1024> request{};
    const int length = std::snprintf(
        request.data(), request.size(),
        "GET /api/v1/stream/%llu HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Authorization: Bearer %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: %s\r\n\r\n",
        static_cast<unsigned long long>(order_id), kToken, key ? key : "");
    return length > 0 && static_cast<size_t>(length) < request.size() &&
           send_all(fd, request.data(), static_cast<size_t>(length));
}

[[nodiscard]] bool send_http_request(int fd, const char* method, const char* path,
                                     const char* body, HttpReply& reply) noexcept {
    const size_t body_size = body ? std::strlen(body) : 0;
    std::array<char, 2048> request{};
    const int length = std::snprintf(
        request.data(), request.size(),
        "%s %s HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n%s",
        method, path, kToken, body_size, body ? body : "");
    return length > 0 && static_cast<size_t>(length) < request.size() &&
           send_all(fd, request.data(), static_cast<size_t>(length)) &&
           read_http_reply(fd, reply, true);
}

[[nodiscard]] bool json_i64(const char* json, const char* name,
                            int64_t& output) noexcept {
    if (!json || !name) return false;
    std::array<char, 64> needle{};
    const int needle_size = std::snprintf(needle.data(), needle.size(), "\"%s\"", name);
    if (needle_size <= 0 || static_cast<size_t>(needle_size) >= needle.size()) return false;
    const char* value = std::strstr(json, needle.data());
    if (!value) return false;
    value += needle_size;
    while (*value == ' ' || *value == '\t' || *value == ':') ++value;
    char* end = nullptr;
    const long long parsed = std::strtoll(value, &end, 10);
    if (end == value) return false;
    output = static_cast<int64_t>(parsed);
    return true;
}

[[nodiscard]] uint64_t require_order_id(const HttpReply& reply) {
    int64_t result = 0;
    assert(json_i64(reply.bytes.data(), "order_id", result));
    assert(result > 0);
    return static_cast<uint64_t>(result);
}

struct WsFrame {
    uint8_t opcode = 0;
    bool fin = false;
    bool masked = false;
    std::array<uint8_t, 8192> payload{};
    size_t payload_size = 0;
};

// RFC 6455 server-to-client frames are unmasked.  The test decoder accepts a
// mask only to make a protocol violation visible in an assertion below.
[[nodiscard]] bool receive_frame(int fd, WsFrame& frame,
                                 uint64_t timeout_ns = kShortTimeoutNs) noexcept {
    uint8_t first_two[2]{};
    if (!read_exact(fd, first_two, sizeof(first_two), timeout_ns)) return false;
    frame.fin = (first_two[0] & 0x80U) != 0;
    frame.opcode = first_two[0] & 0x0fU;
    if ((first_two[0] & 0x70U) != 0) return false;
    frame.masked = (first_two[1] & 0x80U) != 0;
    uint64_t payload_size = first_two[1] & 0x7fU;
    if (payload_size == 126U) {
        uint8_t extended[2]{};
        if (!read_exact(fd, extended, sizeof(extended), timeout_ns)) return false;
        payload_size = (static_cast<uint64_t>(extended[0]) << 8U) | extended[1];
    } else if (payload_size == 127U) {
        uint8_t extended[8]{};
        if (!read_exact(fd, extended, sizeof(extended), timeout_ns)) return false;
        payload_size = 0;
        for (uint8_t byte : extended)
            payload_size = (payload_size << 8U) | byte;
    }
    if (payload_size > frame.payload.size()) return false;
    uint8_t mask[4]{};
    if (frame.masked && !read_exact(fd, mask, sizeof(mask), timeout_ns)) return false;
    if (payload_size != 0 &&
        !read_exact(fd, frame.payload.data(), static_cast<size_t>(payload_size), timeout_ns))
        return false;
    if (frame.masked) {
        for (size_t i = 0; i < payload_size; ++i) frame.payload[i] ^= mask[i & 3U];
    }
    frame.payload_size = static_cast<size_t>(payload_size);
    if (frame.payload_size < frame.payload.size()) frame.payload[frame.payload_size] = 0;
    return true;
}

[[nodiscard]] size_t http_header_size(const HttpReply& reply) noexcept {
    const char* const marker = std::strstr(reply.bytes.data(), "\r\n\r\n");
    if (!marker) return 0;
    return static_cast<size_t>(marker - reply.bytes.data()) + 4U;
}

// An HTTP upgrade response and the first WebSocket frame may be returned in
// one recv().  Keep the bytes following CRLFCRLF so the pre-read-frame test
// proves the HTTP-to-WS ownership handoff did not silently discard them.
struct BufferedFrameInput {
    int fd = -1;
    std::array<uint8_t, 8192> retained{};
    size_t retained_offset = 0;
    size_t retained_size = 0;
};

[[nodiscard]] BufferedFrameInput retained_http_input(int fd,
                                                      const HttpReply& reply) noexcept {
    BufferedFrameInput input{};
    input.fd = fd;
    const size_t header_size = http_header_size(reply);
    assert(header_size != 0 && header_size <= reply.size);
    input.retained_size = reply.size - header_size;
    assert(input.retained_size <= input.retained.size());
    if (input.retained_size != 0) {
        std::memcpy(input.retained.data(), reply.bytes.data() + header_size,
                    input.retained_size);
    }
    return input;
}

[[nodiscard]] bool buffered_read_exact(BufferedFrameInput& input,
                                       void* destination, size_t count,
                                       uint64_t timeout_ns = kShortTimeoutNs) noexcept {
    auto* bytes = static_cast<uint8_t*>(destination);
    const uint64_t deadline = monotonic_ns() + timeout_ns;
    while (count != 0) {
        const size_t retained = input.retained_size - input.retained_offset;
        if (retained != 0) {
            const size_t copied = std::min(retained, count);
            std::memcpy(bytes, input.retained.data() + input.retained_offset, copied);
            input.retained_offset += copied;
            bytes += copied;
            count -= copied;
            continue;
        }
        if (!wait_readable(input.fd, deadline)) return false;
        const ssize_t received = ::recv(input.fd, bytes, count, 0);
        if (received > 0) {
            bytes += received;
            count -= static_cast<size_t>(received);
            continue;
        }
        if (received < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

[[nodiscard]] bool receive_frame(BufferedFrameInput& input, WsFrame& frame,
                                 uint64_t timeout_ns = kShortTimeoutNs) noexcept {
    uint8_t first_two[2]{};
    if (!buffered_read_exact(input, first_two, sizeof(first_two), timeout_ns)) return false;
    frame.fin = (first_two[0] & 0x80U) != 0;
    frame.opcode = first_two[0] & 0x0fU;
    if ((first_two[0] & 0x70U) != 0) return false;
    frame.masked = (first_two[1] & 0x80U) != 0;
    uint64_t payload_size = first_two[1] & 0x7fU;
    if (payload_size == 126U) {
        uint8_t extended[2]{};
        if (!buffered_read_exact(input, extended, sizeof(extended), timeout_ns)) return false;
        payload_size = (static_cast<uint64_t>(extended[0]) << 8U) | extended[1];
    } else if (payload_size == 127U) {
        uint8_t extended[8]{};
        if (!buffered_read_exact(input, extended, sizeof(extended), timeout_ns)) return false;
        payload_size = 0;
        for (const uint8_t byte : extended)
            payload_size = (payload_size << 8U) | byte;
    }
    if (payload_size > frame.payload.size()) return false;
    uint8_t mask[4]{};
    if (frame.masked &&
        !buffered_read_exact(input, mask, sizeof(mask), timeout_ns)) return false;
    if (payload_size != 0 &&
        !buffered_read_exact(input, frame.payload.data(),
                             static_cast<size_t>(payload_size), timeout_ns))
        return false;
    if (frame.masked) {
        for (size_t index = 0; index < payload_size; ++index)
            frame.payload[index] ^= mask[index & 3U];
    }
    frame.payload_size = static_cast<size_t>(payload_size);
    if (frame.payload_size < frame.payload.size()) frame.payload[frame.payload_size] = 0;
    return true;
}

// Client-to-server frames must carry a four-byte mask.  Keeping this encoder
// here exercises the server's actual RFC 6455 masking parser rather than a
// test-only shortcut.
[[nodiscard]] bool send_masked_frame(int fd, uint8_t opcode,
                                     const uint8_t* payload, size_t payload_size) noexcept {
    if (payload_size > 125U) return false;
    std::array<uint8_t, 256> bytes{};
    constexpr std::array<uint8_t, 4> mask{0x31U, 0x57U, 0x91U, 0xc3U};
    bytes[0] = static_cast<uint8_t>(0x80U | (opcode & 0x0fU));
    bytes[1] = static_cast<uint8_t>(0x80U | payload_size);
    std::memcpy(bytes.data() + 2, mask.data(), mask.size());
    for (size_t i = 0; i < payload_size; ++i)
        bytes[6 + i] = payload[i] ^ mask[i & 3U];
    return send_all(fd, bytes.data(), 6 + payload_size);
}

[[nodiscard]] bool send_masked_frame(int fd, uint8_t opcode,
                                     const char* payload) noexcept {
    return send_masked_frame(fd, opcode,
                             reinterpret_cast<const uint8_t*>(payload),
                             payload ? std::strlen(payload) : 0);
}

[[nodiscard]] bool send_split_upgrade_with_masked_ping(int fd,
                                                        uint64_t order_id) noexcept {
    std::array<char, 1024> request{};
    const int request_size = std::snprintf(
        request.data(), request.size(),
        "GET /api/v1/stream/%llu HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Authorization: Bearer %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: %s\r\n\r\n",
        static_cast<unsigned long long>(order_id), kToken, kWebSocketKey);
    if (request_size <= 0 || static_cast<size_t>(request_size) >= request.size()) {
        return false;
    }
    const char* const key_header = std::strstr(request.data(), "Sec-WebSocket-Key:");
    if (!key_header) return false;
    // Split inside the final header, then put the rest of the HTTP request and
    // a masked client ping in one write. This forces both incomplete-header
    // accumulation and post-header byte retention to be exercised.
    const size_t split = static_cast<size_t>(key_header - request.data()) + 8U;
    if (!send_all(fd, request.data(), split)) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    constexpr std::array<uint8_t, 7> kMaskedPing{
        0x89U, 0x81U, 0x31U, 0x57U, 0x91U, 0xc3U,
        static_cast<uint8_t>('p' ^ 0x31U),
    };
    std::array<uint8_t, 1200> suffix{};
    const size_t request_tail = static_cast<size_t>(request_size) - split;
    assert(request_tail + kMaskedPing.size() <= suffix.size());
    std::memcpy(suffix.data(), request.data() + split, request_tail);
    std::memcpy(suffix.data() + request_tail, kMaskedPing.data(), kMaskedPing.size());
    return send_all(fd, suffix.data(), request_tail + kMaskedPing.size());
}

[[nodiscard]] bool wait_for_eof(int fd, uint64_t timeout_ns = kShortTimeoutNs) noexcept {
    const uint64_t deadline = monotonic_ns() + timeout_ns;
    while (wait_readable(fd, deadline)) {
        char scratch[256]{};
        const ssize_t received = ::recv(fd, scratch, sizeof(scratch), 0);
        if (received == 0) return true;
        if (received < 0 && errno != EINTR) return false;
    }
    return false;
}

struct ServerStorage {
    alignas(luv::ws::Server) std::array<std::byte, sizeof(luv::ws::Server)> bytes{};
    alignas(luv::ws::Server::connection_storage_alignment())
        std::array<std::byte,
                   luv::ws::Server::connection_pool_storage_bytes()>
            connection_bytes{};
    bool occupied = false;

    [[nodiscard]] std::span<std::byte> connection_storage() noexcept {
        return {connection_bytes.data(), connection_bytes.size()};
    }
};

// The production server's fixed 1000-slot pool can be several MiB.  Keeping
// this test-only owner in static storage avoids turning test stack limits into
// a false protocol failure while preserving pre-allocation.
ServerStorage g_server_storage{};

[[nodiscard]] luv::ws::Server* construct_server(uint32_t max_connections,
                                                  uint64_t ping_interval_ns,
                                                  uint64_t pong_timeout_ns) {
    assert(!g_server_storage.occupied);
    g_server_storage.occupied = true;
    return std::construct_at(reinterpret_cast<luv::ws::Server*>(
        g_server_storage.bytes.data()), max_connections, ping_interval_ns,
        pong_timeout_ns);
}

void destroy_server(luv::ws::Server*& server) noexcept {
    if (!server) return;
    server->stop();
    std::destroy_at(server);
    server = nullptr;
    g_server_storage.occupied = false;
}

struct ExecutionHarness {
    luv::Arena arena;
    luv::ExecutionGateway gateway;
    uint64_t next_direct_order_id = 1'000'000ULL;

    ExecutionHarness() {
        assert(arena.init());
        assert(gateway.init(arena));
        luv::exec::RiskLimits limits{};
        limits.max_order_qty = 100'000;
        limits.max_abs_position = 10'000'000;
        limits.max_alpha_age_ns = kSecondNs;
        for (uint16_t symbol = 0; symbol < luv::Config::kSymbols; ++symbol)
            assert(gateway.risk().set_limits(symbol, limits));
    }

    [[nodiscard]] uint64_t add_order(uint16_t symbol, int64_t quantity = 100,
                                     int64_t price = 1'000'000) {
        const uint64_t id = next_direct_order_id++;
        const uint64_t timestamp = monotonic_ns();
        luv::exec::OrderIntent intent{};
        intent.symbol_idx = symbol;
        intent.side = luv::exec::kBuy;
        intent.qty = quantity;
        intent.price = price;
        intent.alpha_timestamp_ns = timestamp;
        intent.now_ns = timestamp;
        intent.client_order_id = static_cast<uint32_t>(id);
        luv::OutboundPacket packet{};
        assert(gateway.try_build(intent, packet).pass == 1);
        return id;
    }

    // Each HttpServer starts its request-ID counter at one.  Tests share one
    // execution gateway to avoid repeatedly clearing the arena's 840 MiB
    // infrastructure slab, so completed slots are retired between fixtures
    // before a later server reuses an ID.  Reconciliation has already removed
    // terminal entries; no live execution-thread state is touched here.
    void clear_terminal_orders() noexcept {
        for (luv::SymbolExecState& state : arena.exec_states) {
            for (luv::ActiveOrder& order : state.orders) {
                if (order.state == 3) order = luv::ActiveOrder{};
            }
        }
    }
};

struct NetworkFixture {
    ExecutionHarness& execution;
    luv::http::ExecutionBridge bridge{};
    luv::http::BearerKeyStore keys{};
    luv::ws::Server* websocket = nullptr;
    std::optional<luv::http::HttpServer> http;

    explicit NetworkFixture(ExecutionHarness& execution_ref,
                            uint32_t max_connections = luv::ws::kMaxConnections,
                            uint64_t ping_interval_ns = 30 * kSecondNs,
                            uint64_t pong_timeout_ns = 30 * kSecondNs)
        : execution(execution_ref) {
        execution.clear_terminal_orders();
        assert(keys.add(kToken));
        websocket = construct_server(max_connections, ping_interval_ns,
                                     pong_timeout_ns);
        assert(websocket->start(g_server_storage.connection_storage()));
        execution.gateway.set_fill_event_callback(
            &luv::ws::Server::publish_callback, websocket);
        http.emplace(bridge, keys,
                     luv::http::ServerConfig{.bind_address = "127.0.0.1", .port = 0},
                     websocket);
        assert(http->start());
    }

    ~NetworkFixture() {
        execution.gateway.set_fill_event_callback(nullptr, nullptr);
        http.reset();
        destroy_server(websocket);
    }

    NetworkFixture(const NetworkFixture&) = delete;
    NetworkFixture& operator=(const NetworkFixture&) = delete;

    [[nodiscard]] uint16_t port() const noexcept { return http->port(); }

    void process_pending() noexcept {
        while (bridge.process_one(execution.gateway)) {}
    }

    [[nodiscard]] uint64_t submit_rest(uint16_t symbol, int64_t quantity,
                                       int64_t price) {
        std::array<char, 256> body{};
        const int body_size = std::snprintf(
            body.data(), body.size(),
            "{\"symbol_idx\":%u,\"side\":\"buy\",\"qty\":%lld,\"price\":%lld}",
            static_cast<unsigned>(symbol), static_cast<long long>(quantity),
            static_cast<long long>(price));
        assert(body_size > 0 && static_cast<size_t>(body_size) < body.size());
        HttpReply reply{};
        std::thread client([&] {
            const int fd = connect_loopback(port());
            assert(fd >= 0);
            assert(send_http_request(fd, "POST", "/api/v1/orders", body.data(), reply));
            int closing_fd = fd;
            close_fd(closing_fd);
        });
        client.join();
        assert(reply.starts_with("HTTP/1.1 202 Accepted"));
        const uint64_t id = require_order_id(reply);
        assert(wait_until([&] {
            process_pending();
            luv::ActiveOrder order{};
            return execution.gateway.query_order(id, order);
        }));
        return id;
    }
};

[[nodiscard]] int open_websocket(NetworkFixture& fixture, uint64_t order_id,
                                  const char* key = kWebSocketKey) {
    const int fd = connect_loopback(fixture.port());
    assert(fd >= 0);
    assert(send_upgrade_request(fd, order_id, key));
    HttpReply reply{};
    assert(read_http_reply(fd, reply, false));
    assert(reply.starts_with("HTTP/1.1 101 Switching Protocols"));
    assert(reply.contains("Upgrade: websocket"));
    assert(reply.contains("Connection: Upgrade"));
    assert(reply.contains(kWebSocketAccept));
    return fd;
}

void assert_fill_frame(const WsFrame& frame, uint64_t order_id,
                       int64_t filled_quantity, int64_t price) {
    assert(frame.fin);
    assert(frame.opcode == 0x1U);
    assert(!frame.masked);
    assert(std::strstr(reinterpret_cast<const char*>(frame.payload.data()),
                       "\"event\":\"fill\"") != nullptr);
    int64_t value = 0;
    assert(json_i64(reinterpret_cast<const char*>(frame.payload.data()), "order_id", value));
    assert(value == static_cast<int64_t>(order_id));
    assert(json_i64(reinterpret_cast<const char*>(frame.payload.data()), "filled_qty", value));
    assert(value == filled_quantity);
    assert(json_i64(reinterpret_cast<const char*>(frame.payload.data()), "fill_price", value));
    assert(value == price);
    assert(json_i64(reinterpret_cast<const char*>(frame.payload.data()), "timestamp_ns", value));
    assert(value > 0);
}

struct LatencyStats {
    uint64_t p50_ns = 0;
    uint64_t p99_ns = 0;
    uint64_t max_ns = 0;
};

template <size_t N>
[[nodiscard]] LatencyStats summarize(std::array<uint64_t, N>& samples,
                                     size_t count) {
    assert(count != 0 && count <= samples.size());
    std::sort(samples.begin(), samples.begin() + static_cast<ptrdiff_t>(count));
    return LatencyStats{.p50_ns = samples[count / 2],
                        .p99_ns = samples[(count * 99U) / 100U],
                        .max_ns = samples[count - 1]};
}

template <typename Function>
void run_case(const char* name, Function&& function) {
    const uint64_t start = monotonic_ns();
    function();
    const uint64_t elapsed = monotonic_ns() - start;
    std::printf("[OK] %-44s %8.3f ms\n", name,
                static_cast<double>(elapsed) / 1'000'000.0);
}

template <size_t N>
void close_clients(std::array<int, N>& clients) noexcept {
    for (int& client : clients) close_fd(client);
}

void test_rest_to_websocket_fill(ExecutionHarness& execution) {
    NetworkFixture fixture(execution, 32);
    const uint64_t order_id = fixture.submit_rest(2, 100, 1'000'000);
    int client = open_websocket(fixture, order_id);
    assert(wait_until([&] { return fixture.websocket->active_connections() == 1; }));

    const uint64_t before = fixture.websocket->completed_broadcasts();
    assert(execution.gateway.apply_execution_report(
        2, luv::ExecutionReport{order_id, 100, true}));
    assert(wait_until([&] {
        return fixture.websocket->completed_broadcasts() > before;
    }));

    WsFrame frame{};
    assert(receive_frame(client, frame));
    assert_fill_frame(frame, order_id, 100, 1'000'000);
    const auto diagnostics = fixture.websocket->diagnostics();
    assert(diagnostics.last_fanout_count == 1);
    assert(diagnostics.last_broadcast_latency_ns < 100'000ULL);
    std::printf("      fill->JSON queued=%llu ns\n",
                static_cast<unsigned long long>(diagnostics.last_broadcast_latency_ns));

    close_fd(client);
    assert(wait_until([&] { return fixture.websocket->active_connections() == 0; }));
}

template <size_t Samples>
[[nodiscard]] LatencyStats run_ten_subscriber_benchmark(
    ExecutionHarness& execution, size_t sample_count, bool verify_unsubscribed) {
    assert(sample_count > 0 && sample_count <= Samples);
    NetworkFixture fixture(execution, 32);
    constexpr uint64_t order_id = 7'001;
    std::array<int, 10> subscribers{};
    subscribers.fill(-1);
    for (int& subscriber : subscribers) subscriber = open_websocket(fixture, order_id);
    int unrelated = -1;
    if (verify_unsubscribed) unrelated = open_websocket(fixture, order_id + 1);
    const uint32_t expected_connections =
        static_cast<uint32_t>(subscribers.size() + (verify_unsubscribed ? 1U : 0U));
    assert(wait_until([&] {
        return fixture.websocket->active_connections() == expected_connections;
    }));

    std::array<uint64_t, Samples> samples{};
    for (size_t i = 0; i < sample_count; ++i) {
        const uint64_t completed = fixture.websocket->completed_broadcasts();
        const luv::ExecutionGateway::FillEvent event{
            .order_id = order_id,
            .filled_qty = static_cast<int64_t>(i + 1),
            .fill_price = 1'000'000,
            .timestamp_ns = utc_ns(),
        };
        assert(fixture.websocket->publish(event));
        assert(wait_until([&] {
            return fixture.websocket->completed_broadcasts() > completed;
        }));
        const auto diagnostics = fixture.websocket->diagnostics();
        assert(diagnostics.last_fanout_count == subscribers.size());
        samples[i] = diagnostics.last_broadcast_latency_ns;
        for (int subscriber : subscribers) {
            WsFrame frame{};
            assert(receive_frame(subscriber, frame));
            assert_fill_frame(frame, order_id, static_cast<int64_t>(i + 1), 1'000'000);
        }
    }

    if (verify_unsubscribed) {
        pollfd poll_fd{.fd = unrelated, .events = POLLIN, .revents = 0};
        assert(::poll(&poll_fd, 1, 10) == 0);
    }
    close_clients(subscribers);
    close_fd(unrelated);
    assert(wait_until([&] { return fixture.websocket->active_connections() == 0; }));

    const LatencyStats stats = summarize(samples, sample_count);
    assert(stats.p99_ns < 100'000ULL);
    return stats;
}

void test_ten_subscriber_fanout(ExecutionHarness& execution) {
    const LatencyStats stats = run_ten_subscriber_benchmark<128>(execution, 128, true);
    std::printf("      fanout=10 p50=%llu ns p99=%llu ns max=%llu ns\n",
                static_cast<unsigned long long>(stats.p50_ns),
                static_cast<unsigned long long>(stats.p99_ns),
                static_cast<unsigned long long>(stats.max_ns));
}

template <size_t Samples>
[[nodiscard]] LatencyStats run_hundred_subscriber_benchmark(
    ExecutionHarness& execution, size_t sample_count) {
    constexpr size_t kSubscribers = 100;
    assert(sample_count > 0 && sample_count <= Samples);
    NetworkFixture fixture(execution, 128);
    constexpr uint64_t order_id = 7'101;
    std::array<int, kSubscribers> subscribers{};
    subscribers.fill(-1);
    for (int& subscriber : subscribers) subscriber = open_websocket(fixture, order_id);
    assert(wait_until([&] {
        return fixture.websocket->active_connections() == kSubscribers;
    }, kLongTimeoutNs));

    std::array<uint64_t, Samples> samples{};
    for (size_t sample = 0; sample < sample_count; ++sample) {
        const uint64_t completed = fixture.websocket->completed_broadcasts();
        assert(fixture.websocket->publish({
            .order_id = order_id,
            .filled_qty = static_cast<int64_t>(sample + 1),
            .fill_price = 1'000'000,
            .timestamp_ns = utc_ns(),
        }));
        assert(wait_until([&] {
            return fixture.websocket->completed_broadcasts() > completed;
        }));
        const auto diagnostics = fixture.websocket->diagnostics();
        assert(diagnostics.last_fanout_count == kSubscribers);
        samples[sample] = diagnostics.last_broadcast_latency_ns;
        for (const int subscriber : subscribers) {
            WsFrame frame{};
            assert(receive_frame(subscriber, frame));
            assert_fill_frame(frame, order_id, static_cast<int64_t>(sample + 1),
                              1'000'000);
        }
    }

    close_clients(subscribers);
    assert(wait_until([&] { return fixture.websocket->active_connections() == 0; },
                      kLongTimeoutNs));
    const LatencyStats stats = summarize(samples, sample_count);
    assert(stats.p99_ns < 100'000ULL);
    return stats;
}

void test_hundred_subscriber_fanout(ExecutionHarness& execution) {
    const LatencyStats stats = run_hundred_subscriber_benchmark<512>(execution, 512);
    std::printf("      fanout=100 p50=%llu ns p99=%llu ns max=%llu ns\n",
                static_cast<unsigned long long>(stats.p50_ns),
                static_cast<unsigned long long>(stats.p99_ns),
                static_cast<unsigned long long>(stats.max_ns));
}

void test_one_hundred_order_fill_burst(ExecutionHarness& execution) {
    constexpr size_t kOrders = 100;
    // Exercise the configured 1,000-slot pool, not a smaller benchmark-only
    // instance. The 900 unrelated subscribers would make a flat pool scan
    // dominate every fill; the fixed per-order index must keep the 100-order
    // burst independent of them.
    constexpr size_t kBackgroundSubscribers =
        luv::ws::kMaxConnections - kOrders;
    NetworkFixture fixture(execution);
    std::array<uint64_t, kOrders> order_ids{};
    std::array<uint16_t, kOrders> symbols{};
    std::array<int, luv::ws::kMaxConnections> clients{};
    clients.fill(-1);
    for (size_t i = 0; i < kOrders; ++i) {
        symbols[i] = static_cast<uint16_t>(20 + i);
        order_ids[i] = execution.add_order(symbols[i], 10, 1'000'000 + static_cast<int64_t>(i));
        clients[i] = open_websocket(fixture, order_ids[i]);
    }
    for (size_t i = 0; i < kBackgroundSubscribers; ++i) {
        clients[kOrders + i] = open_websocket(fixture, 90'000);
    }
    assert(wait_until([&] {
        return fixture.websocket->active_connections() == luv::ws::kMaxConnections;
    }, kLongTimeoutNs));

    const uint64_t completed_before = fixture.websocket->completed_broadcasts();
    const uint64_t start = monotonic_ns();
    for (size_t i = 0; i < kOrders; ++i) {
        assert(execution.gateway.apply_execution_report(
            symbols[i], luv::ExecutionReport{order_ids[i], 10, true}));
    }
    assert(wait_until([&] {
        return fixture.websocket->completed_broadcasts() >= completed_before + kOrders;
    }, kLongTimeoutNs));
    for (size_t i = 0; i < kOrders; ++i) {
        WsFrame frame{};
        assert(receive_frame(clients[i], frame));
        assert_fill_frame(frame, order_ids[i], 10, 1'000'000 + static_cast<int64_t>(i));
    }
    const uint64_t elapsed = monotonic_ns() - start;
    assert(elapsed != 0);
    const uint64_t throughput = (kOrders * kSecondNs) / elapsed;
    std::array<uint64_t, kOrders> queued_latencies{};
    for (size_t i = 0; i < kOrders; ++i) {
        queued_latencies[i] = fixture.websocket->broadcast_latency_sample(
            completed_before + i);
    }
    const LatencyStats latency = summarize(queued_latencies, queued_latencies.size());
    std::printf("      100-order aggregate throughput=%llu fills/s\n",
                static_cast<unsigned long long>(throughput));
    std::printf("      default-pool burst p50=%llu ns p99=%llu ns max=%llu ns\n",
                static_cast<unsigned long long>(latency.p50_ns),
                static_cast<unsigned long long>(latency.p99_ns),
                static_cast<unsigned long long>(latency.max_ns));
    std::fflush(stdout);
    assert(latency.p99_ns < 100'000ULL);

    close_clients(clients);
    assert(wait_until([&] { return fixture.websocket->active_connections() == 0; }));
}

void test_slow_client_backpressure(ExecutionHarness& execution) {
    NetworkFixture fixture(execution, 1);
    int sockets[2]{-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    int small_buffer = 512;
    assert(::setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF, &small_buffer,
                        sizeof(small_buffer)) == 0);
    assert(::setsockopt(sockets[1], SOL_SOCKET, SO_RCVBUF, &small_buffer,
                        sizeof(small_buffer)) == 0);
    assert(fixture.websocket->enqueue_upgrade(sockets[0], 8'001, kWebSocketKey) ==
           luv::ws::UpgradeResult::kAccepted);
    sockets[0] = -1;  // worker owns its endpoint after a successful enqueue.

    HttpReply handshake{};
    assert(read_http_reply(sockets[1], handshake, false));
    assert(handshake.starts_with("HTTP/1.1 101 Switching Protocols"));
    assert(wait_until([&] { return fixture.websocket->active_connections() == 1; }));

    const uint64_t slow_before = fixture.websocket->diagnostics().slow_client_closes;
    // Do not drain sockets[1].  A finite application buffer plus the tiny
    // kernel send buffer must turn this into a slow-subscriber close rather
    // than a block on the execution-side publish call.
    for (uint32_t i = 0; i < 4'096 &&
                         fixture.websocket->active_connections() != 0; ++i) {
        const luv::ExecutionGateway::FillEvent event{
            .order_id = 8'001,
            .filled_qty = static_cast<int64_t>(i + 1),
            .fill_price = 1'000'000,
            .timestamp_ns = utc_ns(),
        };
        if (!fixture.websocket->publish(event))
            std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    assert(wait_until([&] {
        return fixture.websocket->diagnostics().slow_client_closes > slow_before;
    }, kLongTimeoutNs));
    const auto diagnostics = fixture.websocket->diagnostics();
    assert(diagnostics.last_backpressure_close_ns < 10'000'000ULL);
    assert(fixture.websocket->active_connections() == 0);
    std::printf("      backpressure detect->close=%llu ns\n",
                static_cast<unsigned long long>(diagnostics.last_backpressure_close_ns));
    close_fd(sockets[1]);
}

void test_disconnect_cleanup(ExecutionHarness& execution) {
    NetworkFixture fixture(execution, 4);
    constexpr uint64_t order_id = 8'101;
    int client = open_websocket(fixture, order_id);
    const uint64_t cleanup_before = fixture.websocket->diagnostics().cleanup_count;
    close_fd(client);
    assert(wait_until([&] {
        return fixture.websocket->active_connections() == 0 &&
               fixture.websocket->diagnostics().cleanup_count > cleanup_before;
    }));

    const luv::ExecutionGateway::FillEvent later_fill{
        .order_id = order_id,
        .filled_qty = 1,
        .fill_price = 1'000'000,
        .timestamp_ns = utc_ns(),
    };
    assert(fixture.websocket->publish(later_fill));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    assert(fixture.websocket->active_connections() == 0);
}

void test_pool_exhaustion(ExecutionHarness& execution) {
    constexpr size_t kCapacity = 100;
    NetworkFixture fixture(execution, kCapacity);
    std::array<int, kCapacity> clients{};
    clients.fill(-1);
    for (size_t i = 0; i < kCapacity; ++i)
        clients[i] = open_websocket(fixture, 9'000 + i);
    assert(wait_until([&] {
        return fixture.websocket->active_connections() == kCapacity;
    }, kLongTimeoutNs));

    int exhausted = connect_loopback(fixture.port());
    assert(exhausted >= 0);
    assert(send_upgrade_request(exhausted, 9'999, kWebSocketKey));
    HttpReply reply{};
    assert(read_http_reply(exhausted, reply, true));
    assert(reply.starts_with("HTTP/1.1 503 Service Unavailable"));
    assert(reply.contains("Connection pool exhausted"));
    close_fd(exhausted);

    close_fd(clients[0]);
    assert(wait_until([&] { return fixture.websocket->active_connections() == kCapacity - 1; }));
    clients[0] = open_websocket(fixture, 9'999);
    assert(wait_until([&] { return fixture.websocket->active_connections() == kCapacity; }));
    close_clients(clients);
    assert(wait_until([&] { return fixture.websocket->active_connections() == 0; },
                      kLongTimeoutNs));
}

void test_rfc6455_handshake_and_masking(ExecutionHarness& execution) {
    NetworkFixture fixture(execution, 4);
    int client = open_websocket(fixture, 10'001);
    // A valid, masked client data frame must be unmasked and consumed as data,
    // not misclassified as an unmasked control frame or a protocol error.
    assert(send_masked_frame(client, 0x1U, "masked client data"));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    assert(fixture.websocket->active_connections() == 1);
    close_fd(client);
    assert(wait_until([&] { return fixture.websocket->active_connections() == 0; }));

    int invalid = connect_loopback(fixture.port());
    assert(invalid >= 0);
    assert(send_upgrade_request(invalid, 10'002, "invalid-key"));
    HttpReply reply{};
    assert(read_http_reply(invalid, reply, true));
    assert(reply.starts_with("HTTP/1.1 400 Bad Request"));
    assert(reply.contains("Invalid WebSocket key"));
    close_fd(invalid);
}

void test_split_upgrade_and_retained_masked_ping(ExecutionHarness& execution) {
    NetworkFixture fixture(execution, 4);
    int client = connect_loopback(fixture.port());
    assert(client >= 0);
    assert(send_split_upgrade_with_masked_ping(client, 10'003));

    HttpReply reply{};
    assert(read_http_reply(client, reply, false));
    assert(reply.starts_with("HTTP/1.1 101 Switching Protocols"));
    assert(reply.contains(kWebSocketAccept));

    BufferedFrameInput input = retained_http_input(client, reply);
    WsFrame pong{};
    assert(receive_frame(input, pong));
    // The client ping was entirely present after the HTTP delimiter.  A pong
    // here proves those bytes survived the HTTP->WS ownership transfer and
    // were parsed as a masked RFC 6455 control frame by the worker.
    assert(pong.fin && pong.opcode == 0x0aU && !pong.masked);
    assert(pong.payload_size == 1 && pong.payload[0] == 'p');
    assert(fixture.websocket->active_connections() == 1);
    close_fd(client);
    assert(wait_until([&] { return fixture.websocket->active_connections() == 0; }));
}

void test_ping_pong_keepalive(ExecutionHarness& execution) {
    // Exercise the public default exactly as deployed: the first keepalive
    // must arrive 30–35 seconds after the connection is established, then a
    // valid masked pong keeps that connection alive.  The short fixture below
    // separately validates timeout behavior without another long wait.
    {
        constexpr uint64_t kDefaultLowerBoundNs = 30 * kSecondNs;
        constexpr uint64_t kDefaultUpperBoundNs = 35 * kSecondNs;
        NetworkFixture default_fixture(execution, 4);
        const uint64_t connected_at = monotonic_ns();
        int default_client = open_websocket(default_fixture, 10'999);
        const uint64_t pongs_before =
            default_fixture.websocket->diagnostics().pongs_received;
        WsFrame default_ping{};
        assert(receive_frame(default_client, default_ping,
                             kDefaultUpperBoundNs + 2 * kSecondNs));
        const uint64_t elapsed = monotonic_ns() - connected_at;
        assert(default_ping.fin && default_ping.opcode == 0x09U &&
               !default_ping.masked);
        assert(elapsed >= kDefaultLowerBoundNs);
        assert(elapsed <= kDefaultUpperBoundNs);
        assert(send_masked_frame(default_client, 0x0aU,
                                 default_ping.payload.data(),
                                 default_ping.payload_size));
        assert(wait_until([&] {
            return default_fixture.websocket->diagnostics().pongs_received >
                   pongs_before;
        }));
        assert(default_fixture.websocket->active_connections() == 1);
        std::printf("      default ping arrived after %.3f s\n",
                    static_cast<double>(elapsed) / static_cast<double>(kSecondNs));
        close_fd(default_client);
        assert(wait_until([&] {
            return default_fixture.websocket->active_connections() == 0;
        }));
    }

    constexpr uint64_t kPingIntervalNs = 20'000'000ULL;
    constexpr uint64_t kPongTimeoutNs = 100'000'000ULL;
    NetworkFixture fixture(execution, 4, kPingIntervalNs, kPongTimeoutNs);

    int responsive = open_websocket(fixture, 11'001);
    const uint64_t pongs_before = fixture.websocket->diagnostics().pongs_received;
    WsFrame ping{};
    assert(receive_frame(responsive, ping, kShortTimeoutNs));
    assert(ping.fin && ping.opcode == 0x09U && !ping.masked);
    assert(send_masked_frame(responsive, 0x0aU, ping.payload.data(), ping.payload_size));
    assert(wait_until([&] {
        return fixture.websocket->diagnostics().pongs_received > pongs_before;
    }));
    const auto responsive_diagnostics = fixture.websocket->diagnostics();
    assert(responsive_diagnostics.last_pong_rtt_ns < kSecondNs);
    // This deterministic period exercises the no-pong timeout state machine
    // after the literal default-period check above.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(fixture.websocket->active_connections() == 1);
    close_fd(responsive);
    assert(wait_until([&] { return fixture.websocket->active_connections() == 0; }));

    int silent = open_websocket(fixture, 11'002);
    WsFrame silent_ping{};
    assert(receive_frame(silent, silent_ping, kShortTimeoutNs));
    assert(silent_ping.opcode == 0x09U);
    // Deliberately omit a pong: server-side timeout must clean up the peer.
    assert(wait_until([&] { return fixture.websocket->active_connections() == 0; },
                      kShortTimeoutNs));
    assert(wait_for_eof(silent, kShortTimeoutNs));
    close_fd(silent);
}

void test_text_fill_frame(ExecutionHarness& execution) {
    NetworkFixture fixture(execution, 4);
    const uint64_t order_id = execution.add_order(42, 25, 1'234'500);
    int client = open_websocket(fixture, order_id);
    const uint64_t completed = fixture.websocket->completed_broadcasts();
    assert(execution.gateway.apply_execution_report(
        42, luv::ExecutionReport{order_id, 25, true}));
    assert(wait_until([&] {
        return fixture.websocket->completed_broadcasts() > completed;
    }));
    WsFrame frame{};
    assert(receive_frame(client, frame));
    assert(frame.opcode == 0x01U);
    assert_fill_frame(frame, order_id, 25, 1'234'500);
    close_fd(client);
    assert(wait_until([&] { return fixture.websocket->active_connections() == 0; }));
}

void test_close_handshake(ExecutionHarness& execution) {
    NetworkFixture fixture(execution, 4);
    int client = open_websocket(fixture, 12'001);
    constexpr std::array<uint8_t, 2> kNormalClosure{0x03U, 0xe8U};
    assert(send_masked_frame(client, 0x08U, kNormalClosure.data(),
                             kNormalClosure.size()));
    // A peer may stop writing immediately after its close frame.  The server
    // must still flush its close echo before the TCP half-close becomes EOF.
    assert(::shutdown(client, SHUT_WR) == 0);
    WsFrame close_reply{};
    assert(receive_frame(client, close_reply));
    assert(close_reply.fin && close_reply.opcode == 0x08U && !close_reply.masked);
    assert(close_reply.payload_size == 2);
    assert(close_reply.payload[0] == kNormalClosure[0]);
    assert(close_reply.payload[1] == kNormalClosure[1]);
    assert(wait_for_eof(client));
    close_fd(client);
    assert(wait_until([&] { return fixture.websocket->active_connections() == 0; }));
}

void test_connection_pool_churn(ExecutionHarness& execution) {
    constexpr uint32_t kCycles = 1'000;
    NetworkFixture fixture(execution, 8);
    const uint64_t cleanup_before = fixture.websocket->diagnostics().cleanup_count;
    for (uint32_t cycle = 0; cycle < kCycles; ++cycle) {
        int client = open_websocket(fixture, 13'000 + cycle);
        close_fd(client);
        assert(wait_until([&] { return fixture.websocket->active_connections() == 0; }));
    }
    const auto diagnostics = fixture.websocket->diagnostics();
    assert(diagnostics.cleanup_count >= cleanup_before + kCycles);
    assert(luv::ws::Server::connection_storage_bytes() <= 10U * 1024U);
    std::printf("      cleanup cycles=%u connection storage=%zu bytes\n", kCycles,
                luv::ws::Server::connection_storage_bytes());
}

void test_connection_storage_validation() {
    luv::ws::Server* server = construct_server(2, kSecondNs, kSecondNs);
    alignas(luv::ws::Server::connection_storage_alignment())
        std::array<std::byte, luv::ws::Server::connection_storage_bytes()>
            undersized{};
    assert(!server->start(
        {undersized.data(), undersized.size()}));

    std::span<std::byte> storage = g_server_storage.connection_storage();
    assert(storage.size() > 1U);
    assert(!server->start({storage.data() + 1U, storage.size() - 1U}));
    assert(server->start(storage));
    destroy_server(server);
}

void test_stop_racing_upgrade_enqueue() {
    // HttpServer is the one SPSC producer in production. Exercise that exact
    // one-producer ownership while stop() concurrently revokes admission;
    // accepted descriptors must be drained before the pool can be unmapped.
    luv::ws::Server* server = construct_server(16, kSecondNs, kSecondNs);
    assert(server->start(g_server_storage.connection_storage()));

    std::atomic<bool> producer_started{false};
    std::atomic<bool> stop_complete{false};
    std::atomic<uint32_t> submitted{0};
    std::thread producer([&] {
        producer_started.store(true, std::memory_order_release);
        for (uint32_t index = 0; index < 50'000U &&
             !stop_complete.load(std::memory_order_acquire); ++index) {
            int pair[2]{-1, -1};
            assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
            const luv::ws::UpgradeResult result = server->enqueue_upgrade(
                pair[0], 20'000U + index, kWebSocketKey);
            // enqueue_upgrade owns pair[0] on every result path; this peer
            // descriptor is test-owned and makes accepted upgrades clean up.
            close_fd(pair[1]);
            assert(result == luv::ws::UpgradeResult::kAccepted ||
                   result == luv::ws::UpgradeResult::kPoolExhausted ||
                   result == luv::ws::UpgradeResult::kQueueFull);
            submitted.fetch_add(1U, std::memory_order_release);
            std::this_thread::yield();
        }
    });
    assert(wait_until([&] {
        return producer_started.load(std::memory_order_acquire) &&
               submitted.load(std::memory_order_acquire) >= 8U;
    }));
    server->stop();
    stop_complete.store(true, std::memory_order_release);
    producer.join();
    assert(server->active_connections() == 0);
    destroy_server(server);
}

void test_stop_racing_fill_publish() {
    // Execution is the sole SPSC fill producer in production. Exercise its
    // lifecycle boundary independently of upgrade admission: a producer
    // racing stop() must either enqueue before revocation or return false,
    // never leave a stale event for a later start or touch unmapped storage.
    luv::ws::Server* server = construct_server(16, kSecondNs, kSecondNs);
    assert(server->start(g_server_storage.connection_storage()));
    std::atomic<uint32_t> attempts{0};
    std::atomic<bool> stop_complete{false};
    std::thread producer([&] {
        uint32_t index = 0;
        while (!stop_complete.load(std::memory_order_acquire)) {
            (void)server->publish({
                .order_id = 30'000U + index,
                .filled_qty = 1,
                .fill_price = 1'000'000,
                .timestamp_ns = utc_ns(),
            });
            attempts.fetch_add(1U, std::memory_order_release);
            ++index;
        }
    });
    assert(wait_until([&] {
        return attempts.load(std::memory_order_acquire) >= 8U;
    }));
    server->stop();
    stop_complete.store(true, std::memory_order_release);
    producer.join();
    assert(!server->publish({
        .order_id = 99'999,
        .filled_qty = 1,
        .fill_price = 1'000'000,
        .timestamp_ns = utc_ns(),
    }));
    assert(server->active_connections() == 0);
    destroy_server(server);
}

void test_partial_fill_cumulative_quantity(ExecutionHarness& execution) {
    NetworkFixture fixture(execution, 4);
    const uint64_t order_id = execution.add_order(70, 100, 1'111'000);
    int client = open_websocket(fixture, order_id);

    uint64_t completed = fixture.websocket->completed_broadcasts();
    assert(execution.gateway.apply_execution_report(
        70, luv::ExecutionReport{order_id, 50, false}));
    assert(wait_until([&] {
        return fixture.websocket->completed_broadcasts() > completed;
    }));
    WsFrame first{};
    assert(receive_frame(client, first));
    assert_fill_frame(first, order_id, 50, 1'111'000);
    int64_t first_timestamp = 0;
    assert(json_i64(reinterpret_cast<const char*>(first.payload.data()),
                    "timestamp_ns", first_timestamp));

    completed = fixture.websocket->completed_broadcasts();
    assert(execution.gateway.apply_execution_report(
        70, luv::ExecutionReport{order_id, 30, false}));
    assert(wait_until([&] {
        return fixture.websocket->completed_broadcasts() > completed;
    }));
    WsFrame second{};
    assert(receive_frame(client, second));
    // The public stream uses cumulative filled quantity, not the 30-share
    // delta in the second venue report.
    assert_fill_frame(second, order_id, 80, 1'111'000);
    int64_t second_timestamp = 0;
    assert(json_i64(reinterpret_cast<const char*>(second.payload.data()),
                    "timestamp_ns", second_timestamp));
    assert(second_timestamp >= first_timestamp);

    assert(execution.gateway.apply_execution_report(
        70, luv::ExecutionReport{order_id, 20, true}));
    close_fd(client);
    assert(wait_until([&] { return fixture.websocket->active_connections() == 0; }));
}

struct ConcurrentSubmit {
    std::array<char, 256> body{};
    HttpReply reply{};
};

void test_concurrent_http_and_websocket(ExecutionHarness& execution) {
    constexpr size_t kOrders = 100;
    constexpr size_t kSubscribersPerOrder = 10;
    constexpr size_t kSubscribers = kOrders * kSubscribersPerOrder;
    NetworkFixture fixture(execution, static_cast<uint32_t>(kSubscribers));

    // WebSocket routes do not consume REST order IDs.  Establish subscriptions
    // first so every accepted HTTP order (1..100 in this fresh fixture) has
    // ten ready listeners while the independent HTTP and WS workers run.
    std::array<int, kSubscribers> subscribers{};
    subscribers.fill(-1);
    for (size_t order = 0; order < kOrders; ++order) {
        for (size_t subscriber = 0; subscriber < kSubscribersPerOrder; ++subscriber) {
            subscribers[(order * kSubscribersPerOrder) + subscriber] =
                open_websocket(fixture, order + 1U);
        }
    }
    assert(wait_until([&] {
        return fixture.websocket->active_connections() == kSubscribers;
    }, kLongTimeoutNs));

    std::array<ConcurrentSubmit, kOrders> submits{};
    for (size_t index = 0; index < kOrders; ++index) {
        const int length = std::snprintf(
            submits[index].body.data(), submits[index].body.size(),
            "{\"symbol_idx\":%u,\"side\":\"buy\",\"qty\":10,\"price\":%lld}",
            static_cast<unsigned>(100 + index),
            static_cast<long long>(1'500'000 + index));
        assert(length > 0 && static_cast<size_t>(length) < submits[index].body.size());
    }

    std::atomic<bool> start{false};
    std::atomic<uint32_t> finished{0};
    std::array<std::thread, kOrders> clients{};
    for (size_t index = 0; index < kOrders; ++index) {
        clients[index] = std::thread([&, index] {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            const int fd = connect_loopback_retry(fixture.port(), kLongTimeoutNs);
            assert(fd >= 0);
            assert(send_http_request(fd, "POST", "/api/v1/orders",
                                     submits[index].body.data(), submits[index].reply));
            int closing_fd = fd;
            close_fd(closing_fd);
            finished.fetch_add(1U, std::memory_order_release);
        });
    }
    start.store(true, std::memory_order_release);
    assert(wait_until([&] {
        fixture.process_pending();
        return finished.load(std::memory_order_acquire) == kOrders;
    }, kLongTimeoutNs));
    for (std::thread& client : clients) client.join();
    fixture.process_pending();

    std::array<bool, kOrders + 1U> seen{};
    for (const ConcurrentSubmit& submit : submits) {
        assert(submit.reply.starts_with("HTTP/1.1 202 Accepted"));
        const uint64_t order_id = require_order_id(submit.reply);
        assert(order_id >= 1U && order_id <= kOrders);
        assert(!seen[order_id]);
        seen[order_id] = true;
    }
    for (size_t order = 1; order <= kOrders; ++order) assert(seen[order]);
    assert(wait_until([&] {
        fixture.process_pending();
        for (uint64_t order_id = 1; order_id <= kOrders; ++order_id) {
            luv::ActiveOrder order{};
            if (!execution.gateway.query_order(order_id, order)) return false;
        }
        return true;
    }, kLongTimeoutNs));

    std::array<int64_t, kOrders + 1U> price_by_order_id{};
    for (uint64_t order_id = 1; order_id <= kOrders; ++order_id) {
        luv::ActiveOrder order{};
        assert(execution.gateway.query_order(order_id, order));
        price_by_order_id[order_id] = order.price;
    }

    const uint64_t queued_before = fixture.websocket->diagnostics().queued_frames;
    const uint64_t completed_before = fixture.websocket->completed_broadcasts();
    for (uint64_t order_id = 1; order_id <= kOrders; ++order_id) {
        luv::ActiveOrder order{};
        assert(execution.gateway.query_order(order_id, order));
        assert(execution.gateway.apply_execution_report(
            static_cast<uint16_t>(order.symbol_idx),
            luv::ExecutionReport{order_id, 10, true}));
    }
    assert(wait_until([&] {
        return fixture.websocket->completed_broadcasts() >= completed_before + kOrders;
    }, kLongTimeoutNs));
    assert(wait_until([&] {
        return fixture.websocket->diagnostics().queued_frames >=
               queued_before + kSubscribers;
    }, kLongTimeoutNs));

    for (size_t order = 0; order < kOrders; ++order) {
        for (size_t subscriber = 0; subscriber < kSubscribersPerOrder; ++subscriber) {
            WsFrame frame{};
            const int fd = subscribers[(order * kSubscribersPerOrder) + subscriber];
            assert(receive_frame(fd, frame, kLongTimeoutNs));
            assert_fill_frame(frame, order + 1U, 10,
                              price_by_order_id[order + 1U]);
        }
    }
    close_clients(subscribers);
    assert(wait_until([&] { return fixture.websocket->active_connections() == 0; },
                      kLongTimeoutNs));
}

template <size_t Samples>
[[nodiscard]] LatencyStats run_single_subscriber_benchmark(
    ExecutionHarness& execution, size_t sample_count) {
    assert(sample_count > 0 && sample_count <= Samples);
    NetworkFixture fixture(execution, 4);
    constexpr uint64_t order_id = 15'001;
    int client = open_websocket(fixture, order_id);
    std::array<uint64_t, Samples> samples{};
    for (size_t index = 0; index < sample_count; ++index) {
        const uint64_t before = fixture.websocket->completed_broadcasts();
        assert(fixture.websocket->publish({
            .order_id = order_id,
            .filled_qty = static_cast<int64_t>(index + 1),
            .fill_price = 1'000'000,
            .timestamp_ns = utc_ns(),
        }));
        assert(wait_until([&] {
            return fixture.websocket->completed_broadcasts() > before;
        }));
        samples[index] = fixture.websocket->diagnostics().last_broadcast_latency_ns;
        WsFrame frame{};
        assert(receive_frame(client, frame));
        assert_fill_frame(frame, order_id, static_cast<int64_t>(index + 1),
                          1'000'000);
    }
    close_fd(client);
    assert(wait_until([&] { return fixture.websocket->active_connections() == 0; }));
    const LatencyStats stats = summarize(samples, sample_count);
    assert(stats.p99_ns < 100'000ULL);
    return stats;
}

void run_microbenchmarks(ExecutionHarness& execution, size_t samples) {
    const LatencyStats fill = run_single_subscriber_benchmark<512>(execution, samples);
    const LatencyStats fanout = run_ten_subscriber_benchmark<512>(execution, samples, false);
    const LatencyStats fanout_hundred =
        run_hundred_subscriber_benchmark<512>(execution, samples);
    std::printf("WebSocket latency microbenchmark (%zu samples)\n", samples);
    std::printf("  fill -> JSON queued: p50=%llu ns p99=%llu ns max=%llu ns\n",
                static_cast<unsigned long long>(fill.p50_ns),
                static_cast<unsigned long long>(fill.p99_ns),
                static_cast<unsigned long long>(fill.max_ns));
    std::printf("  fanout (10) queued: p50=%llu ns p99=%llu ns max=%llu ns\n",
                static_cast<unsigned long long>(fanout.p50_ns),
                static_cast<unsigned long long>(fanout.p99_ns),
                static_cast<unsigned long long>(fanout.max_ns));
    std::printf("  fanout (100) queued: p50=%llu ns p99=%llu ns max=%llu ns\n",
                static_cast<unsigned long long>(fanout_hundred.p50_ns),
                static_cast<unsigned long long>(fanout_hundred.p99_ns),
                static_cast<unsigned long long>(fanout_hundred.max_ns));
}

}  // namespace

int main(int argc, char** argv) {
    const bool benchmark_only = argc == 2 && std::strcmp(argv[1], "--bench") == 0;
    if (argc > 2 || (argc == 2 && !benchmark_only)) {
        std::fprintf(stderr, "Usage: %s [--bench]\n", argv[0]);
        return 2;
    }

    ExecutionHarness execution;
    if (benchmark_only) {
        run_case("fill, ten-, and hundred-subscriber latency microbenchmarks", [&] {
            run_microbenchmarks(execution, 512);
        });
        run_case("slow-client backpressure microbenchmark", [&] {
            test_slow_client_backpressure(execution);
        });
        std::puts("WebSocket microbenchmarks passed");
        return 0;
    }

    run_case("1 REST order -> WebSocket fill delivery", [&] {
        test_rest_to_websocket_fill(execution);
    });
    run_case("fill callback -> JSON queue latency", [&] {
        const LatencyStats stats = run_single_subscriber_benchmark<128>(execution, 128);
        std::printf("      fill p50=%llu ns p99=%llu ns max=%llu ns\n",
                    static_cast<unsigned long long>(stats.p50_ns),
                    static_cast<unsigned long long>(stats.p99_ns),
                    static_cast<unsigned long long>(stats.max_ns));
    });
    run_case("2 fanout to ten and one hundred subscribers", [&] {
        test_ten_subscriber_fanout(execution);
        test_hundred_subscriber_fanout(execution);
    });
    run_case("3 one hundred concurrent order fills", [&] {
        test_one_hundred_order_fill_burst(execution);
    });
    run_case("4 slow-client backpressure close", [&] {
        test_slow_client_backpressure(execution);
    });
    run_case("5 disconnect cleanup", [&] {
        test_disconnect_cleanup(execution);
    });
    run_case("6 connection-pool exhaustion", [&] {
        test_pool_exhaustion(execution);
    });
    run_case("7 RFC 6455 handshake and masking", [&] {
        test_rfc6455_handshake_and_masking(execution);
        test_split_upgrade_and_retained_masked_ping(execution);
    });
    run_case("8 ping/pong keepalive and timeout", [&] {
        test_ping_pong_keepalive(execution);
    });
    run_case("9 text JSON fill frame", [&] {
        test_text_fill_frame(execution);
    });
    run_case("10 normal close handshake", [&] {
        test_close_handshake(execution);
    });
    run_case("11 one thousand connection churn cycles", [&] {
        test_connection_pool_churn(execution);
    });
    run_case("Arena-compatible WebSocket storage validation", [&] {
        test_connection_storage_validation();
    });
    run_case("upgrade admission racing worker shutdown", [&] {
        test_stop_racing_upgrade_enqueue();
    });
    run_case("fill publication racing worker shutdown", [&] {
        test_stop_racing_fill_publish();
    });
    run_case("12 cumulative partial fill notifications", [&] {
        test_partial_fill_cumulative_quantity(execution);
    });
    run_case("13 concurrent HTTP plus WebSocket fanout", [&] {
        test_concurrent_http_and_websocket(execution);
    });

    std::puts("All WebSocket scenarios passed");
    return 0;
}
