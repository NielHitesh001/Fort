#include "luv_http_server.hpp"
#include "luv_websocket.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

namespace luv::http {
namespace {
uint64_t now_ns() noexcept { timespec ts{}; ::clock_gettime(CLOCK_MONOTONIC, &ts); return uint64_t(ts.tv_sec) * 1'000'000'000ULL + uint64_t(ts.tv_nsec); }
// Response IDs cross the execution/HTTP SPSC boundary.  Keeping this source
// global prevents a reply left by a stopped server instance from colliding
// with the first request accepted by a replacement listener.
std::atomic<uint64_t> g_next_http_request_id{1};
uint64_t take_http_request_id() noexcept {
    uint64_t id = g_next_http_request_id.load(std::memory_order_relaxed);
    while (id != 0) {
        const uint64_t next = id == UINT64_MAX ? 0 : id + 1U;
        if (g_next_http_request_id.compare_exchange_weak(
                id, next, std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            return id;
        }
    }
    return 0;
}
const char* name(OrderStatus s) noexcept { switch (s) { case OrderStatus::kPending: return "pending"; case OrderStatus::kLive: return "live"; case OrderStatus::kPartial: return "partial"; case OrderStatus::kCancelled: return "cancelled"; case OrderStatus::kDone: return "done"; case OrderStatus::kRejected: return "rejected"; case OrderStatus::kNotFound: return "not_found"; } return "not_found"; }
bool number(const char* text, uint64_t& out) noexcept { if (!text || *text < '0' || *text > '9') return false; uint64_t value = 0; while (*text >= '0' && *text <= '9') { const uint64_t digit = uint64_t(*text++ - '0'); if (value > (UINT64_MAX - digit) / 10) return false; value = value * 10 + digit; } out = value; return true; }
const char* value(const char* body, const char* key) noexcept { char needle[48]{}; const int n = std::snprintf(needle, sizeof(needle), "\"%s\"", key); if (n <= 0 || size_t(n) >= sizeof(needle)) return nullptr; const char* found = std::strstr(body, needle); if (!found) return nullptr; found += n; while (*found == ' ' || *found == '\t' || *found == ':') ++found; return found; }
bool json_value_end(const char* text) noexcept {
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
    return *text == ',' || *text == '}' || *text == '\0';
}
bool json_uint64(const char* text, uint64_t& out) noexcept {
    if (!text || *text < '0' || *text > '9') return false;
    uint64_t parsed = 0;
    while (*text >= '0' && *text <= '9') {
        const uint64_t digit = static_cast<uint64_t>(*text++ - '0');
        if (parsed > (UINT64_MAX - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
    }
    if (!json_value_end(text)) return false;
    out = parsed;
    return true;
}
bool json_side(const char* text, uint8_t& side) noexcept {
    if (!text || *text != '\"') return false;
    ++text;
    if (std::strncmp(text, "buy\"", 4) == 0) {
        text += 4;
        side = exec::kBuy;
    } else if (std::strncmp(text, "sell\"", 5) == 0) {
        text += 5;
        side = exec::kSell;
    } else {
        return false;
    }
    return json_value_end(text);
}
bool json_decimal_price(const char* text, int64_t& out) noexcept {
    // API decimal prices are converted directly to the engine's x10^4
    // integer representation.  Parsing digits avoids a lossy double roundtrip.
    if (!text || *text < '0' || *text > '9') return false;
    uint64_t whole = 0;
    while (*text >= '0' && *text <= '9') {
        const uint64_t digit = static_cast<uint64_t>(*text++ - '0');
        if (whole > (UINT64_MAX - digit) / 10U) return false;
        whole = whole * 10U + digit;
    }

    uint64_t fractional = 0;
    uint32_t fractional_digits = 0;
    if (*text == '.') {
        ++text;
        while (*text >= '0' && *text <= '9') {
            if (fractional_digits == 4U) return false;
            fractional = fractional * 10U +
                static_cast<uint64_t>(*text++ - '0');
            ++fractional_digits;
        }
        // A trailing decimal point is not a decimal value in this API.
        if (fractional_digits == 0U) return false;
    }
    if (!json_value_end(text)) return false;
    while (fractional_digits < 4U) {
        fractional *= 10U;
        ++fractional_digits;
    }
    if (whole > (static_cast<uint64_t>(INT64_MAX) - fractional) / 10'000U)
        return false;
    out = static_cast<int64_t>(whole * 10'000U + fractional);
    return true;
}
bool symbol_char(char value) noexcept {
    return (value >= 'A' && value <= 'Z') ||
        (value >= 'a' && value <= 'z') ||
        (value >= '0' && value <= '9') || value == '.' || value == '-';
}
char symbol_upper(char value) noexcept {
    return value >= 'a' && value <= 'z'
        ? static_cast<char>(value - 'a' + 'A')
        : value;
}
bool json_symbol_index(const char* text, uint16_t& out) noexcept {
    if (!text || *text++ != '\"') return false;

    // HTTP has no mutable instrument registry on its I/O thread.  A stable
    // FNV-1a mapping gives a bounded, allocation-free simulation index for a
    // ticker; callers that require venue-grade symbol identity must use the
    // engine's registered numeric symbol_idx form instead.
    uint32_t hash = 2'166'136'261U;
    uint32_t length = 0;
    while (*text && *text != '\"') {
        if (length == 8U || !symbol_char(*text)) return false;
        hash ^= static_cast<uint8_t>(symbol_upper(*text++));
        hash *= 16'777'619U;
        ++length;
    }
    if (*text != '\"' || length == 0U || !json_value_end(text + 1)) return false;
    out = static_cast<uint16_t>(hash % Config::kSymbols);
    return true;
}
bool submit_order(const char* body, exec::OrderIntent& order) noexcept {
    const char* symbol_idx = value(body, "symbol_idx");
    const char* symbol = value(body, "symbol");
    const char* side = value(body, "side");
    const char* qty_text = value(body, "qty");
    const char* price_text = value(body, "price");
    uint64_t parsed_symbol = 0;
    uint64_t qty = 0;
    int64_t price = 0;

    // Exactly one symbol representation is required.  The legacy numeric
    // price remains fixed-point; the ticker form treats price as decimal USD.
    if ((symbol_idx == nullptr) == (symbol == nullptr) ||
        !json_uint64(qty_text, qty) || qty > INT64_MAX ||
        !json_side(side, order.side)) return false;
    if (symbol_idx) {
        uint64_t legacy_price = 0;
        if (!json_uint64(symbol_idx, parsed_symbol) ||
            parsed_symbol >= Config::kSymbols ||
            !json_uint64(price_text, legacy_price) || legacy_price > INT64_MAX)
            return false;
        order.symbol_idx = static_cast<uint16_t>(parsed_symbol);
        price = static_cast<int64_t>(legacy_price);
    } else if (!json_symbol_index(symbol, order.symbol_idx) ||
               !json_decimal_price(price_text, price)) {
        return false;
    }

    order.qty = static_cast<int64_t>(qty);
    order.price = price;
    order.alpha_timestamp_ns = now_ns();
    order.now_ns = order.alpha_timestamp_ns;
    return true;
}
// This direct response is used only after the fixed 64-client table is full,
// so there is deliberately no admitted slot in which to retain a continuation.
// It is a bounded best-effort rejection; every accepted client uses the
// per-slot POLLOUT state machine below instead.
void reject_unowned_connection(const int fd, const int status,
                               const char* reason, const char* body) noexcept {
    char response[256]{};
    const size_t body_size = std::strlen(body);
    const int size = std::snprintf(
        response, sizeof(response),
        "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
        status, reason, body_size, body);
    if (size > 0 && static_cast<size_t>(size) < sizeof(response)) {
        (void)::send(fd, response, static_cast<size_t>(size),
                     MSG_NOSIGNAL | MSG_DONTWAIT);
    }
}

// HTTP field names and token values used by the WebSocket upgrade are ASCII
// case-insensitive.  Keep parsing bounded by the received header block rather
// than relying on an unbounded substring search.
struct HeaderRange { const char* begin = nullptr; const char* end = nullptr; };
char ascii_lower(char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}
bool ascii_equal(const char* left, size_t left_size,
                 const char* right) noexcept {
    const size_t right_size = std::strlen(right);
    if (left_size != right_size) return false;
    for (size_t i = 0; i < left_size; ++i)
        if (ascii_lower(left[i]) != ascii_lower(right[i])) return false;
    return true;
}
HeaderRange header(const char* request, const char* block_end,
                   const char* wanted_name) noexcept {
    const char* line = std::strstr(request, "\r\n");
    while (line && line < block_end) {
        line += 2;
        if (line >= block_end) break;
        const char* line_end = std::strstr(line, "\r\n");
        if (!line_end || line_end > block_end) break;
        const char* colon = line;
        while (colon < line_end && *colon != ':') ++colon;
        if (colon < line_end && ascii_equal(line, size_t(colon - line), wanted_name)) {
            const char* value_begin = colon + 1;
            while (value_begin < line_end && (*value_begin == ' ' || *value_begin == '\t')) ++value_begin;
            const char* value_end = line_end;
            while (value_end > value_begin &&
                   (value_end[-1] == ' ' || value_end[-1] == '\t')) --value_end;
            return {value_begin, value_end};
        }
        line = line_end;
    }
    return {};
}
bool header_has_token(HeaderRange value, const char* token) noexcept {
    while (value.begin && value.begin < value.end) {
        while (value.begin < value.end &&
               (*value.begin == ' ' || *value.begin == '\t' || *value.begin == ',')) ++value.begin;
        const char* token_end = value.begin;
        while (token_end < value.end && *token_end != ',') ++token_end;
        const char* trimmed_end = token_end;
        while (trimmed_end > value.begin &&
               (trimmed_end[-1] == ' ' || trimmed_end[-1] == '\t')) --trimmed_end;
        if (ascii_equal(value.begin, size_t(trimmed_end - value.begin), token)) return true;
        value.begin = token_end < value.end ? token_end + 1 : value.end;
    }
    return false;
}
bool header_decimal_size(HeaderRange value, size_t& output) noexcept {
    if (!value.begin || value.begin == value.end) return false;
    size_t parsed = 0;
    for (const char* current = value.begin; current < value.end; ++current) {
        if (*current < '0' || *current > '9') return false;
        const size_t digit = static_cast<size_t>(*current - '0');
        if (parsed > (SIZE_MAX - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
    }
    output = parsed;
    return true;
}
bool path_number(const char* path, const char* prefix, uint64_t& id) noexcept {
    const size_t prefix_size = std::strlen(prefix);
    if (std::strncmp(path, prefix, prefix_size) != 0) return false;
    const char* text = path + prefix_size;
    if (!number(text, id)) return false;
    while (*text >= '0' && *text <= '9') ++text;
    return *text == '\0';
}
bool order_id(const char* path, uint64_t& id) noexcept {
    return path_number(path, "/api/v1/orders/", id);
}
[[nodiscard]] bool set_nonblocking(const int fd) noexcept {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
}  // namespace

bool BearerKeyStore::add(const char* token) noexcept {
    if (!token) return false; const size_t length = ::strnlen(token, kMaxBearerTokenBytes); if (!length || length >= kMaxBearerTokenBytes) return false;
    for (Key& key : keys_) if (!key.length) { key.length = uint8_t(length); std::memcpy(key.value, token, length); return true; } return false;
}
bool BearerKeyStore::validate(const char* token, size_t length) const noexcept {
    if (!token || !length || length >= kMaxBearerTokenBytes) return false; uint8_t match = 0;
    for (const Key& key : keys_) { uint8_t diff = uint8_t(key.length ^ length); for (size_t i = 0; i < kMaxBearerTokenBytes; ++i) { const uint8_t actual = i < length ? uint8_t(token[i]) : 0; diff |= uint8_t(key.value[i]) ^ actual; } match |= uint8_t(diff == 0 && key.length != 0); } return match != 0;
}
bool ExecutionBridge::submit(const Request& request) noexcept { return requests_.try_push(request); }
bool ExecutionBridge::try_take_response(Response& response) noexcept { return responses_.try_pop(response); }
OrderSnapshot ExecutionBridge::snapshot(const ActiveOrder& order) noexcept {
    OrderSnapshot result{.order_id = order.order_id, .price = order.price, .quantity = order.qty + order.filled_qty, .filled_quantity = order.filled_qty, .symbol_idx = uint16_t(order.symbol_idx), .side = order.side};
    result.status = order.state == 1 ? OrderStatus::kLive : order.state == 2 ? OrderStatus::kPartial : order.state == 3 ? OrderStatus::kDone : OrderStatus::kNotFound; return result;
}
bool ExecutionBridge::process_one(ExecutionGateway& gateway,
                                  OutboundPacket* outbound,
                                  AcceptedSubmission* accepted) noexcept {
    if (accepted) *accepted = {};
    // A completed response must be admitted before this owner consumes the
    // next request.  Dropping it would strand the HTTP client and corrupt the
    // fixed outstanding-response accounting on the other side of the SPSC
    // bridge.
    bool flushed_deferred_response = false;
    if (has_deferred_response_) {
        if (!responses_.try_push(deferred_response_)) return false;
        has_deferred_response_ = false;
        flushed_deferred_response = true;
    }

    Request request{};
    if (!requests_.try_pop(request)) return flushed_deferred_response;

    // POST /orders is acknowledged when it enters the request queue and does
    // not need an execution-to-HTTP response.  Avoid clearing a 12 KiB
    // positions-capable Response object on this hot control path.
    if (request.command == Command::kSubmit) {
        OutboundPacket local{};
        const auto decision = gateway.try_build(
            request.order, outbound ? *outbound : local);
        if (accepted && decision.pass) {
            *accepted = {.order_id = request.order.client_order_id,
                         .quantity = request.order.qty,
                         .symbol_idx = request.order.symbol_idx,
                         .accepted = true};
        }
        return true;
    }

    Response response{};
    response.request_id = request.request_id;
    switch (request.command) {
    case Command::kQuery: { ActiveOrder order{}; response.order = gateway.query_order(request.order_id, order) ? snapshot(order) : OrderSnapshot{.order_id = request.order_id}; break; }
    case Command::kCancel: response.order.order_id = request.order_id; response.order.status = gateway.cancel_order(request.order_id) ? OrderStatus::kCancelled : OrderStatus::kNotFound; break;
    case Command::kPositions: for (uint16_t symbol = 0; symbol < Config::kSymbols; ++symbol) { RiskState state{}; if (!gateway.query_position(symbol, state)) break; if (state.net_position || state.gross_exposure) response.positions[response.position_count++] = {symbol, state.net_position, state.gross_exposure}; } break;
    case Command::kSubmit: break;
    }
    if (!responses_.try_push(response)) {
        deferred_response_ = response;
        has_deferred_response_ = true;
    }
    return true;
}

HttpServer::HttpServer(ExecutionBridge& bridge, const BearerKeyStore& keys,
                       ServerConfig config, ws::Server* websocket) noexcept
    : bridge_(bridge), keys_(keys), config_(config),
      next_order_id_(config.first_order_id), websocket_(websocket) {}
HttpServer::~HttpServer() { stop(); }
bool HttpServer::start() {
    if (running_.load(std::memory_order_acquire) || thread_.joinable() ||
        !config_.bind_address) {
        return false;
    }
    // A stopped listener may have left execution replies in the shared bridge.
    // Their globally unique IDs are deliberately ignored by the next start;
    // do not let them reserve this new listener's fixed response capacity.
    outstanding_response_ids_.fill(0);
    positions_body_size_ = 0;
    positions_body_owner_ = kNoClientSlot;
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return false;
    if (!set_nonblocking(listener)) {
        (void)::close(listener);
        return false;
    }
    fd_.store(listener, std::memory_order_release);
    int reuse = 1;
    (void)::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);
    if (::inet_pton(AF_INET, config_.bind_address, &address.sin_addr) != 1 ||
        ::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
        // The control-plane listener is single-threaded, but clients can
        // connect concurrently while it accepts completed WS upgrades.
        ::listen(listener, 256)) {
        close_socket();
        return false;
    }
    socklen_t length = sizeof(address);
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length)) {
        close_socket();
        return false;
    }
    bound_port_ = ntohs(address.sin_port);
    running_.store(true, std::memory_order_release);
    try { thread_ = std::thread(&HttpServer::serve, this); } catch (...) { running_.store(false); close_socket(); return false; } return true;
}
void HttpServer::stop() noexcept {
    const bool was_running = running_.exchange(false,
                                               std::memory_order_acq_rel);
    if (was_running) {
        // The HTTP I/O owner alone closes the descriptor after its poll loop
        // exits.  Cross-thread close can otherwise let an unrelated newly
        // opened descriptor reuse the same integer while poll()/accept() is
        // still operating on it. shutdown() merely wakes/interrupts the
        // listener and preserves descriptor ownership until join().
        const int listener = fd_.load(std::memory_order_acquire);
        if (listener >= 0) (void)::shutdown(listener, SHUT_RDWR);
    }
    // serve() can exit on an I/O error and clear running_ before its caller
    // observes it. Always join a live worker so destruction cannot terminate.
    if (thread_.joinable()) thread_.join();
}
void HttpServer::serve() noexcept {
    while (running_.load(std::memory_order_acquire)) {
        const int listener = fd_.load(std::memory_order_acquire);
        if (listener < 0) break;
        std::array<pollfd, kMaxClientSlots + 1U> descriptors{};
        std::array<uint32_t, kMaxClientSlots + 1U> slot_indices{};
        descriptors[0] = {.fd = listener, .events = POLLIN, .revents = 0};
        uint32_t descriptor_count = 1;
        for (uint32_t index = 0; index < kMaxClientSlots; ++index) {
            const ClientSlot& client = clients_[index];
            if (client.fd < 0) continue;
            short events = 0;
            if (client.sending_response) {
                events = POLLOUT;
            } else if (!client.waiting_response) {
                events = POLLIN;
            }
            descriptors[descriptor_count] = {
                .fd = client.fd, .events = events, .revents = 0};
            slot_indices[descriptor_count] = index;
            ++descriptor_count;
        }

        // A short bounded poll lets stop(), response delivery, and per-client
        // deadlines progress even when every peer is a slowloris.
        const int ready = ::poll(descriptors.data(), descriptor_count, 10);
        if (ready < 0 && errno != EINTR) break;
        if (ready > 0) {
            if (running_.load(std::memory_order_acquire) &&
                (descriptors[0].revents & POLLIN)) {
                accept_clients(listener);
            }
            for (uint32_t descriptor_index = 1;
                 descriptor_index < descriptor_count; ++descriptor_index) {
                ClientSlot& client = clients_[slot_indices[descriptor_index]];
                if (client.fd < 0) continue;
                const short events = descriptors[descriptor_index].revents;
                if (client.sending_response && (events & POLLOUT)) {
                    flush_client(client);
                }
                if (client.fd >= 0 && !client.sending_response &&
                    !client.waiting_response && (events & (POLLIN | POLLPRI))) {
                    read_client(client);
                }
                if (client.fd >= 0 &&
                    (events & (POLLERR | POLLHUP | POLLNVAL))) {
                    close_client(client);
                }
            }
        }
        drain_responses();
        expire_clients(now_ns());
    }
    close_all_clients();
    close_socket();
    running_.store(false, std::memory_order_release);
}
uint32_t HttpServer::take_order_id() noexcept {
    uint32_t id = next_order_id_.load(std::memory_order_relaxed);
    while (id != 0) {
        // Let the final usable 32-bit venue token through, then leave the
        // atomic at zero permanently.  A later request gets an explicit 503
        // rather than silently aliasing a previous client order ID.
        const uint32_t next = id == UINT32_MAX ? 0U : id + 1U;
        if (next_order_id_.compare_exchange_weak(
                id, next, std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            return id;
        }
    }
    return 0;
}

HttpServer::ClientSlot* HttpServer::acquire_client() noexcept {
    for (ClientSlot& client : clients_) {
        if (client.fd < 0) return &client;
    }
    return nullptr;
}

bool HttpServer::reserve_response_id(const uint64_t request_id) noexcept {
    if (request_id == 0) return false;
    for (uint64_t& outstanding : outstanding_response_ids_) {
        if (outstanding != 0) continue;
        outstanding = request_id;
        return true;
    }
    return false;
}

bool HttpServer::release_response_id(const uint64_t request_id) noexcept {
    if (request_id == 0) return false;
    for (uint64_t& outstanding : outstanding_response_ids_) {
        if (outstanding != request_id) continue;
        outstanding = 0;
        return true;
    }
    return false;
}

bool HttpServer::reserve_positions_body(ClientSlot& client) noexcept {
    if (client.owns_positions_body || positions_body_owner_ != kNoClientSlot) {
        return false;
    }
    const auto index = static_cast<uint32_t>(&client - clients_.data());
    positions_body_owner_ = index;
    positions_body_size_ = 0;
    client.owns_positions_body = true;
    return true;
}

void HttpServer::release_positions_body(ClientSlot& client) noexcept {
    if (!client.owns_positions_body) return;
    const auto index = static_cast<uint32_t>(&client - clients_.data());
    if (positions_body_owner_ == index) {
        positions_body_owner_ = kNoClientSlot;
        positions_body_size_ = 0;
    }
    client.owns_positions_body = false;
    client.positions_body_offset = 0;
}

void HttpServer::close_client(ClientSlot& client) noexcept {
    const int descriptor = client.fd;
    release_positions_body(client);
    client.fd = -1;
    client.deadline_ns = 0;
    client.request_id = 0;
    client.incoming_size = 0;
    client.header_size = 0;
    client.required_size = 0;
    client.command = Command::kSubmit;
    client.header_complete = false;
    client.waiting_response = false;
    client.sending_response = false;
    client.outgoing_size = 0;
    client.outgoing_offset = 0;
    client.positions_body_offset = 0;
    client.incoming[0] = '\0';
    client.outgoing[0] = '\0';
    if (descriptor >= 0) (void)::close(descriptor);
}

void HttpServer::close_all_clients() noexcept {
    for (ClientSlot& client : clients_) close_client(client);
}

void HttpServer::accept_clients(const int listener) noexcept {
    uint32_t accepted = 0;
    while (accepted < kMaxAcceptsPerTurn &&
           running_.load(std::memory_order_acquire)) {
        const int descriptor = ::accept(listener, nullptr, nullptr);
        if (descriptor < 0) {
            if (errno == EINTR) continue;
            return;
        }
        ++accepted;
        if (!set_nonblocking(descriptor)) {
            (void)::close(descriptor);
            continue;
        }
        ClientSlot* const client = acquire_client();
        if (!client) {
            reject_unowned_connection(
                descriptor, 503, "Service Unavailable",
                "{\"error\":\"connection_pool_exhausted\"}");
            (void)::close(descriptor);
            continue;
        }
        client->fd = descriptor;
        client->deadline_ns = now_ns() +
            static_cast<uint64_t>(config_.response_wait_ms) * 1'000'000ULL;
        client->request_id = 0;
        client->incoming_size = 0;
        client->header_size = 0;
        client->required_size = 0;
        client->command = Command::kSubmit;
        client->header_complete = false;
        client->waiting_response = false;
        client->sending_response = false;
        client->owns_positions_body = false;
        client->outgoing_size = 0;
        client->outgoing_offset = 0;
        client->positions_body_offset = 0;
        client->incoming[0] = '\0';
        client->outgoing[0] = '\0';
    }
}

void HttpServer::read_client(ClientSlot& client) noexcept {
    while (client.fd >= 0) {
        const size_t read_limit = client.header_complete
            ? kMaxClientReadBytes : kMaxRequestBytes;
        if (client.incoming_size + 1U >= read_limit) {
            if (!client.header_complete) {
                queue_inline_json(client, 413, "Payload Too Large",
                                  "{\"error\":\"request_too_large\"}");
                return;
            }
            break;
        }
        const ssize_t received = ::recv(
            client.fd, client.incoming.data() + client.incoming_size,
            read_limit - client.incoming_size - 1U, MSG_DONTWAIT);
        if (received > 0) {
            client.incoming_size += static_cast<size_t>(received);
            client.incoming[client.incoming_size] = '\0';
            if (!client.header_complete) {
                const char* const end = std::strstr(client.incoming.data(),
                                                    "\r\n\r\n");
                if (end) {
                    client.header_size = static_cast<size_t>(
                        (end + 4) - client.incoming.data());
                    size_t body_size = 0;
                    const HeaderRange content_length = header(
                        client.incoming.data(), end, "Content-Length");
                    if (content_length.begin &&
                        !header_decimal_size(content_length, body_size)) {
                        queue_inline_json(
                            client, 400, "Bad Request",
                            "{\"error\":\"invalid_content_length\"}");
                        return;
                    }
                    if (client.header_size >= kMaxRequestBytes ||
                        body_size > kMaxRequestBytes - 1U -
                            client.header_size) {
                        queue_inline_json(client, 413, "Payload Too Large",
                                          "{\"error\":\"request_too_large\"}");
                        return;
                    }
                    client.required_size = client.header_size + body_size;
                    client.header_complete = true;
                }
            }
            continue;
        }
        if (received == 0) {
            if (client.header_complete &&
                client.incoming_size >= client.required_size) break;
            close_client(client);
            return;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        close_client(client);
        return;
    }
    if (client.fd >= 0 && client.header_complete &&
        client.incoming_size >= client.required_size) {
        dispatch_client(client);
    }
}

void HttpServer::dispatch_client(ClientSlot& client) noexcept {
    const char* const incoming = client.incoming.data();
    const char* const end = incoming + client.header_size - 4U;
    const HeaderRange authorization = header(incoming, end, "Authorization");
    constexpr char kBearer[] = "Bearer";
    const size_t authorization_size = authorization.begin
        ? static_cast<size_t>(authorization.end - authorization.begin)
        : 0;
    if (!authorization.begin || authorization_size <= sizeof(kBearer) ||
        !ascii_equal(authorization.begin, sizeof(kBearer) - 1U, kBearer) ||
        (authorization.begin[sizeof(kBearer) - 1U] != ' ' &&
         authorization.begin[sizeof(kBearer) - 1U] != '\t')) {
        queue_inline_json(client, 401, "Unauthorized",
                          "{\"error\":\"unauthorized\"}");
        return;
    }
    const char* auth = authorization.begin + sizeof(kBearer) - 1U;
    while (auth < authorization.end && (*auth == ' ' || *auth == '\t')) ++auth;
    if (auth == authorization.end ||
        !keys_.validate(auth, static_cast<size_t>(authorization.end - auth))) {
        queue_inline_json(client, 401, "Unauthorized",
                          "{\"error\":\"unauthorized\"}");
        return;
    }

    char method[8]{}, path[128]{};
    if (std::sscanf(incoming, "%7s %127s HTTP/1.1", method, path) != 2) {
        queue_inline_json(client, 400, "Bad Request",
                          "{\"error\":\"bad_request\"}");
        return;
    }
    if (!std::strcmp(method, "GET") && websocket_ &&
        !std::strncmp(path, "/api/v1/stream/", 15)) {
        uint64_t id = 0;
        const HeaderRange key_header = header(incoming, end, "Sec-WebSocket-Key");
        const HeaderRange version = header(incoming, end, "Sec-WebSocket-Version");
        const bool version_ok = version.begin && ascii_equal(
            version.begin, static_cast<size_t>(version.end - version.begin), "13");
        if (!path_number(path, "/api/v1/stream/", id) || !key_header.begin ||
            !header_has_token(header(incoming, end, "Upgrade"), "websocket") ||
            !header_has_token(header(incoming, end, "Connection"), "Upgrade") ||
            !version_ok) {
            queue_inline_json(client, 400, "Bad Request",
                              "{\"error\":\"invalid_websocket_upgrade\"}");
            return;
        }
        char key[32]{};
        const size_t key_size = static_cast<size_t>(
            key_header.end - key_header.begin);
        if (key_size >= sizeof(key)) {
            queue_inline_json(client, 400, "Bad Request",
                              "{\"error\":\"Invalid WebSocket key\"}");
            return;
        }
        std::memcpy(key, key_header.begin, key_size);
        if (client.required_size != client.header_size ||
            client.incoming_size < client.header_size) {
            queue_inline_json(client, 400, "Bad Request",
                              "{\"error\":\"invalid_websocket_upgrade\"}");
            return;
        }
        const size_t pre_read_size = client.incoming_size - client.header_size;
        if (pre_read_size > ws::kMaxUpgradePreReadBytes) {
            queue_inline_json(client, 400, "Bad Request",
                              "{\"error\":\"invalid_websocket_upgrade\"}");
            return;
        }
        const int retained = ::dup(client.fd);
        const ws::UpgradeResult result = retained < 0
            ? ws::UpgradeResult::kQueueFull
            : websocket_->enqueue_upgrade(retained, id, key, end + 4,
                                          pre_read_size);
        if (result == ws::UpgradeResult::kAccepted) {
            close_client(client);
            return;
        }
        if (result == ws::UpgradeResult::kInvalidKey) {
            queue_inline_json(client, 400, "Bad Request",
                              "{\"error\":\"Invalid WebSocket key\"}");
        } else if (result == ws::UpgradeResult::kInvalidUpgrade) {
            queue_inline_json(client, 400, "Bad Request",
                              "{\"error\":\"invalid_websocket_upgrade\"}");
        } else {
            queue_inline_json(client, 503, "Service Unavailable",
                              "{\"error\":\"Connection pool exhausted\"}");
        }
        return;
    }

    Request command{};
    if (!std::strcmp(method, "POST") && !std::strcmp(path, "/api/v1/orders")) {
        command.command = Command::kSubmit;
        if (!submit_order(end + 4, command.order)) {
            queue_inline_json(client, 400, "Bad Request",
                              "{\"error\":\"invalid_order\"}");
            return;
        }
        command.order.client_order_id = take_order_id();
        if (!command.order.client_order_id) {
            queue_inline_json(client, 503, "Service Unavailable",
                              "{\"error\":\"order_id_exhausted\"}");
            return;
        }
        if (!bridge_.submit(command)) {
            queue_inline_json(client, 503, "Service Unavailable",
                              "{\"error\":\"queue_full\"}");
            return;
        }
        char body[96]{};
        const int written = std::snprintf(
            body, sizeof(body), "{\"order_id\":%u,\"status\":\"accepted\"}",
            command.order.client_order_id);
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(body)) {
            queue_inline_json(client, 500, "Internal Server Error",
                              "{\"error\":\"response_format_failed\"}");
        } else {
            queue_inline_json(client, 202, "Accepted", body);
        }
        return;
    }

    if (!std::strcmp(method, "GET") &&
        !std::strcmp(path, "/api/v1/positions")) {
        command.command = Command::kPositions;
    } else if (!std::strcmp(method, "GET") && order_id(path, command.order_id)) {
        command.command = Command::kQuery;
    } else if (!std::strcmp(method, "DELETE") && order_id(path, command.order_id)) {
        command.command = Command::kCancel;
    } else {
        queue_inline_json(client, 404, "Not Found",
                          "{\"error\":\"not_found\"}");
        return;
    }

    command.request_id = take_http_request_id();
    if (!command.request_id) {
        queue_inline_json(client, 503, "Service Unavailable",
                          "{\"error\":\"request_id_exhausted\"}");
        return;
    }
    if (command.command == Command::kPositions &&
        !reserve_positions_body(client)) {
        queue_inline_json(client, 503, "Service Unavailable",
                          "{\"error\":\"positions_response_busy\"}");
        return;
    }
    if (!reserve_response_id(command.request_id)) {
        release_positions_body(client);
        queue_inline_json(client, 503, "Service Unavailable",
                          "{\"error\":\"response_queue_full\"}");
        return;
    }
    if (!bridge_.submit(command)) {
        (void)release_response_id(command.request_id);
        release_positions_body(client);
        queue_inline_json(client, 503, "Service Unavailable",
                          "{\"error\":\"queue_full\"}");
        return;
    }
    client.request_id = command.request_id;
    client.command = command.command;
    client.waiting_response = true;
    client.deadline_ns = now_ns() +
        static_cast<uint64_t>(config_.response_wait_ms) * 1'000'000ULL;
}

void HttpServer::queue_inline_json(ClientSlot& client, const int status,
                                   const char* reason, const char* body) noexcept {
    if (client.fd < 0 || !reason || !body) return;
    // A timeout/error replacing a positions query must release the shared body
    // reservation immediately. Its eventual execution reply remains safely
    // identifiable in outstanding_response_ids_ and will be discarded.
    release_positions_body(client);
    const size_t body_size = std::strlen(body);
    const int header_size = std::snprintf(
        client.outgoing.data(), client.outgoing.size(),
        "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n",
        status, reason, body_size);
    if (header_size <= 0 ||
        static_cast<size_t>(header_size) >= client.outgoing.size() ||
        body_size > client.outgoing.size() -
            static_cast<size_t>(header_size)) {
        close_client(client);
        return;
    }
    std::memcpy(client.outgoing.data() + header_size, body, body_size);
    client.outgoing_size = static_cast<size_t>(header_size) + body_size;
    client.outgoing_offset = 0;
    client.positions_body_offset = 0;
    client.waiting_response = false;
    client.sending_response = true;
    client.deadline_ns = now_ns() + kResponseWriteTimeoutNs;
    flush_client(client);
}

void HttpServer::queue_positions_json(ClientSlot& client,
                                      const Response& response) noexcept {
    if (client.fd < 0 || !client.owns_positions_body) {
        return;
    }
    // Complete the bounded serializer before the 200 header is queued.  The
    // outgoing state then retains both the header offset and the shared body
    // offset across EAGAIN/POLLOUT cycles; a client can never observe a
    // syntactically valid but silently truncated positions response.
    char* const body = positions_body_.data();
    const size_t body_capacity = positions_body_.size();
    const int prefix = std::snprintf(body, body_capacity, "{\"positions\":[");
    if (prefix < 0 || static_cast<size_t>(prefix) >= body_capacity) {
        queue_inline_json(client, 500, "Internal Server Error",
                          "{\"error\":\"positions_response_too_large\"}");
        return;
    }
    size_t offset = static_cast<size_t>(prefix);
    for (uint16_t i = 0; i < response.position_count; ++i) {
        const auto& position = response.positions[i];
        const int written = std::snprintf(
            body + offset, body_capacity - offset,
            "%s{\"symbol_idx\":%u,\"net_position\":%lld,"
            "\"gross_exposure\":%lld}",
            i ? "," : "", static_cast<unsigned>(position.symbol_idx),
            static_cast<long long>(position.net_position),
            static_cast<long long>(position.gross_exposure));
        if (written < 0 ||
            static_cast<size_t>(written) >= body_capacity - offset) {
            queue_inline_json(client, 500, "Internal Server Error",
                              "{\"error\":\"positions_response_too_large\"}");
            return;
        }
        offset += static_cast<size_t>(written);
    }
    if (offset + 3U > body_capacity) {
        queue_inline_json(client, 500, "Internal Server Error",
                          "{\"error\":\"positions_response_too_large\"}");
        return;
    }
    std::memcpy(body + offset, "]}", 3U);
    positions_body_size_ = offset + 2U;

    const int header_size = std::snprintf(
        client.outgoing.data(), client.outgoing.size(),
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n",
        positions_body_size_);
    if (header_size <= 0 ||
        static_cast<size_t>(header_size) >= client.outgoing.size()) {
        queue_inline_json(client, 500, "Internal Server Error",
                          "{\"error\":\"response_format_failed\"}");
        return;
    }
    client.outgoing_size = static_cast<size_t>(header_size);
    client.outgoing_offset = 0;
    client.positions_body_offset = 0;
    client.waiting_response = false;
    client.sending_response = true;
    client.deadline_ns = now_ns() + kResponseWriteTimeoutNs;
    flush_client(client);
}

void HttpServer::flush_client(ClientSlot& client) noexcept {
    if (client.fd < 0 || !client.sending_response) return;

    const char* bytes = nullptr;
    size_t* offset = nullptr;
    size_t total_size = 0;
    if (client.outgoing_offset < client.outgoing_size) {
        bytes = client.outgoing.data();
        offset = &client.outgoing_offset;
        total_size = client.outgoing_size;
    } else if (client.owns_positions_body &&
               client.positions_body_offset < positions_body_size_) {
        bytes = positions_body_.data();
        offset = &client.positions_body_offset;
        total_size = positions_body_size_;
    } else {
        close_client(client);
        return;
    }

    const size_t configured_chunk = config_.max_write_chunk_bytes == 0
        ? kDefaultWriteChunkBytes
        : static_cast<size_t>(config_.max_write_chunk_bytes);
    const size_t remaining = total_size - *offset;
    const size_t send_size = remaining < configured_chunk
        ? remaining : configured_chunk;
    for (;;) {
        const ssize_t sent = ::send(client.fd, bytes + *offset, send_size,
                                    MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent > 0) {
            *offset += static_cast<size_t>(sent);
            // One bounded write attempt per client per poll turn preserves
            // availability for new REST/WS work under a slow /positions peer.
            return;
        }
        if (sent == 0) {
            close_client(client);
            return;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        close_client(client);
        return;
    }
}

void HttpServer::send_response(ClientSlot& client,
                               const Response& response) noexcept {
    if (client.command == Command::kPositions) {
        queue_positions_json(client, response);
        return;
    }
    char body[256]{};
    const int written = std::snprintf(
        body, sizeof(body),
        "{\"order_id\":%llu,\"status\":\"%s\",\"symbol_idx\":%u,"
        "\"qty\":%lld,\"filled_qty\":%lld,\"price\":%lld}",
        static_cast<unsigned long long>(response.order.order_id),
        name(response.order.status),
        static_cast<unsigned>(response.order.symbol_idx),
        static_cast<long long>(response.order.quantity),
        static_cast<long long>(response.order.filled_quantity),
        static_cast<long long>(response.order.price));
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(body)) {
        queue_inline_json(client, 500, "Internal Server Error",
                          "{\"error\":\"response_format_failed\"}");
        return;
    }
    const bool missing = response.order.status == OrderStatus::kNotFound;
    queue_inline_json(client, missing ? 404 : 200,
                      missing ? "Not Found" : "OK", body);
}

void HttpServer::drain_responses() noexcept {
    Response response{};
    while (bridge_.try_take_response(response)) {
        // A response from an earlier listener instance is not ours. Ignore it
        // without releasing one of this instance's fixed capacity slots.
        if (!release_response_id(response.request_id)) continue;
        for (ClientSlot& client : clients_) {
            if (client.fd < 0 || !client.waiting_response ||
                client.request_id != response.request_id) {
                continue;
            }
            client.waiting_response = false;
            send_response(client, response);
            break;
        }
    }
}

void HttpServer::expire_clients(const uint64_t now) noexcept {
    for (ClientSlot& client : clients_) {
        if (client.fd < 0 || now < client.deadline_ns) continue;
        if (client.sending_response) {
            // The content length makes a peer-visible early close detectable;
            // never block this I/O thread trying to finish an abandoned peer.
            close_client(client);
        } else if (client.waiting_response) {
            queue_inline_json(client, 504, "Gateway Timeout",
                              "{\"error\":\"execution_timeout\"}");
        } else {
            queue_inline_json(client, 408, "Request Timeout",
                              "{\"error\":\"request_timeout\"}");
        }
    }
}

void HttpServer::close_socket() noexcept {
    const int listener = fd_.exchange(-1, std::memory_order_acq_rel);
    if (listener >= 0) {
        (void)::shutdown(listener, SHUT_RDWR);
        (void)::close(listener);
    }
}

}  // namespace luv::http
