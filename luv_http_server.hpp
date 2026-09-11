#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "luv_consumer.hpp"
#include "luv_execution.hpp"

namespace luv::ws { class Server; }
namespace luv::http {

constexpr uint32_t kMaxBearerTokenBytes = 64;
constexpr uint32_t kMaxApiKeys = 32;
// Worst case for every configured symbol with signed 64-bit position and
// exposure values is below 48 KiB today. Keep a fixed 64 KiB control-plane
// buffer so GET /positions can return the complete 512-symbol snapshot
// without heap allocation or partial serialization.
constexpr size_t kMaxPositionsJsonBytes = 64U * 1024U;

class BearerKeyStore {
public:
    [[nodiscard]] bool add(const char* token) noexcept;
    [[nodiscard]] bool validate(const char* token, size_t length) const noexcept;

private:
    struct Key { uint8_t length = 0; char value[kMaxBearerTokenBytes]{}; };
    std::array<Key, kMaxApiKeys> keys_{};
};

enum class Command : uint8_t { kSubmit, kQuery, kCancel, kPositions };
enum class OrderStatus : uint8_t { kPending, kLive, kPartial, kCancelled, kDone, kRejected, kNotFound };

struct OrderSnapshot {
    uint64_t order_id = 0;
    int64_t price = 0;
    int64_t quantity = 0;
    int64_t filled_quantity = 0;
    uint16_t symbol_idx = 0;
    uint8_t side = exec::kBuy;
    OrderStatus status = OrderStatus::kNotFound;
    uint8_t reject_mask = 0;
};
struct PositionSnapshot { uint16_t symbol_idx = 0; int64_t net_position = 0; int64_t gross_exposure = 0; };
struct Request { Command command = Command::kSubmit; uint64_t request_id = 0; uint64_t order_id = 0; exec::OrderIntent order{}; };
// Completion information is returned only to the execution-owner caller of
// process_one().  It never crosses back to the HTTP thread, so a simulator or
// venue adapter can apply its execution report without decoding the outbound
// wire packet or reading execution state from an I/O thread.
struct AcceptedSubmission {
    uint64_t order_id = 0;
    int64_t quantity = 0;
    uint16_t symbol_idx = 0;
    bool accepted = false;
};
struct Response {
    uint64_t request_id = 0;
    OrderSnapshot order{};
    uint16_t position_count = 0;
    std::array<PositionSnapshot, Config::kSymbols> positions{};
};

// Exactly one HTTP-thread producer and one execution-thread consumer own the
// request queue; ownership is reversed for the response queue.
class ExecutionBridge {
public:
    [[nodiscard]] bool submit(const Request& request) noexcept;
    [[nodiscard]] bool try_take_response(Response& response) noexcept;
    [[nodiscard]] bool process_one(ExecutionGateway& gateway,
                                   OutboundPacket* outbound = nullptr,
                                   AcceptedSubmission* accepted = nullptr) noexcept;
private:
    [[nodiscard]] static OrderSnapshot snapshot(const ActiveOrder& order) noexcept;
    StaticSpscQueue<Request, 256> requests_{};
    StaticSpscQueue<Response, 8> responses_{};
    // The execution owner is never allowed to discard a completed query,
    // cancel, or positions snapshot merely because the HTTP consumer has not
    // drained its eight-slot SPSC response ring yet.  This fixed spill slot
    // is retried before another request is consumed; it is not a heap-backed
    // retry queue and preserves SPSC ownership.
    Response deferred_response_{};
    bool has_deferred_response_ = false;
};

struct ServerConfig {
    const char* bind_address = "127.0.0.1";
    uint16_t port = 8080;
    uint32_t response_wait_ms = 100;
    // Bound one nonblocking send attempt so a large control-plane response
    // cannot monopolize an HTTP I/O turn. Tests lower this to exercise
    // buffered partial-write continuation deterministically; zero selects the
    // fixed production default.
    uint32_t max_write_chunk_bytes = 16U * 1024U;
    // Test seam and restart-control setting.  Zero is reserved as an
    // exhausted sentinel; production starts at the default ID of one.
    uint32_t first_order_id = 1;
};

class HttpServer {
public:
    HttpServer(ExecutionBridge& bridge, const BearerKeyStore& keys, ServerConfig config = {}, ws::Server* websocket = nullptr) noexcept;
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    [[nodiscard]] bool start();
    void stop() noexcept;
    [[nodiscard]] uint16_t port() const noexcept { return bound_port_; }
private:
    // The listener owns this fixed client table on its one I/O thread.  A
    // partially received unauthenticated request therefore consumes one slot,
    // never the whole control-plane listener.
    static constexpr uint32_t kMaxClientSlots = 64;
    // Limit accept work per event-loop turn so a completed-connection flood
    // cannot starve already-admitted REST/WS peers or response delivery.
    static constexpr uint32_t kMaxAcceptsPerTurn = 16;
    static constexpr size_t kMaxRequestBytes = 2048;
    // Leave bounded room for a WebSocket frame received with its HTTP
    // upgrade. The worker receives those bytes through the existing upgrade
    // handoff instead of discarding them.
    static constexpr size_t kMaxClientReadBytes = 4096;
    static constexpr uint32_t kMaxPendingExecutionResponses = 8;
    // Ordinary JSON replies are copied into a per-client fixed buffer.  The
    // large /positions body is kept in one separate bounded serializer slab
    // because only one such response may be in flight at a time.
    static constexpr size_t kMaxInlineResponseBytes = 1024;
    static constexpr uint64_t kResponseWriteTimeoutNs =
        5'000'000'000ULL;
    static constexpr size_t kDefaultWriteChunkBytes = 16U * 1024U;
    static constexpr uint32_t kNoClientSlot = UINT32_MAX;

    struct ClientSlot {
        int fd = -1;
        uint64_t deadline_ns = 0;
        uint64_t request_id = 0;
        size_t incoming_size = 0;
        size_t header_size = 0;
        size_t required_size = 0;
        Command command = Command::kSubmit;
        bool header_complete = false;
        bool waiting_response = false;
        bool sending_response = false;
        bool owns_positions_body = false;
        size_t outgoing_size = 0;
        size_t outgoing_offset = 0;
        size_t positions_body_offset = 0;
        std::array<char, kMaxClientReadBytes> incoming{};
        std::array<char, kMaxInlineResponseBytes> outgoing{};
    };

    void serve() noexcept;
    void accept_clients(int listener) noexcept;
    void read_client(ClientSlot& client) noexcept;
    void dispatch_client(ClientSlot& client) noexcept;
    void drain_responses() noexcept;
    void send_response(ClientSlot& client, const Response& response) noexcept;
    void queue_inline_json(ClientSlot& client, int status, const char* reason,
                           const char* body) noexcept;
    void queue_positions_json(ClientSlot& client,
                              const Response& response) noexcept;
    void flush_client(ClientSlot& client) noexcept;
    void expire_clients(uint64_t now) noexcept;
    void close_client(ClientSlot& client) noexcept;
    void close_all_clients() noexcept;
    [[nodiscard]] ClientSlot* acquire_client() noexcept;
    [[nodiscard]] bool reserve_response_id(uint64_t request_id) noexcept;
    [[nodiscard]] bool release_response_id(uint64_t request_id) noexcept;
    [[nodiscard]] bool reserve_positions_body(ClientSlot& client) noexcept;
    void release_positions_body(ClientSlot& client) noexcept;
    // The venue packet token is 32-bit.  Once it wraps, fail closed instead
    // of reusing an order ID and making query/cancel/stream identity unsafe.
    [[nodiscard]] uint32_t take_order_id() noexcept;
    void close_socket() noexcept;
    ExecutionBridge& bridge_;
    const BearerKeyStore& keys_;
    ServerConfig config_{};
    // stop() runs on the caller thread while serve() reads the listener on
    // its dedicated I/O thread.  Keep descriptor ownership race-free.
    std::atomic<int> fd_{-1};
    uint16_t bound_port_ = 0;
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> next_order_id_{1};
    std::thread thread_;
    ws::Server* websocket_ = nullptr;
    // Request IDs are globally unique across server instances.  This fixed
    // table lets a restarted listener discard old bridge replies without
    // accidentally freeing capacity for a different live request.
    std::array<uint64_t, kMaxPendingExecutionResponses>
        outstanding_response_ids_{};
    std::array<ClientSlot, kMaxClientSlots> clients_{};
    // Only the HTTP worker owns this serialization buffer.
    std::array<char, kMaxPositionsJsonBytes> positions_body_{};
    size_t positions_body_size_ = 0;
    uint32_t positions_body_owner_ = kNoClientSlot;
};

}  // namespace luv::http
