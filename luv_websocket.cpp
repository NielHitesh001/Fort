#include "luv_websocket.hpp"

#include <array>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <poll.h>
#if defined(__APPLE__)
#include <sys/event.h>
#endif
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

namespace luv::ws {
namespace {

constexpr char kWebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr size_t kWebSocketKeyBytes = 24;
constexpr size_t kDecodedKeyBytes = 16;
constexpr size_t kSha1Bytes = 20;
constexpr size_t kAcceptBytes = 28;
constexpr size_t kMaxClientFrameHeaderBytes = 14;

[[nodiscard]] uint64_t monotonic_now_ns() noexcept {
    timespec timestamp{};
    (void)::clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return static_cast<uint64_t>(timestamp.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(timestamp.tv_nsec);
}

template <size_t N>
[[nodiscard]] bool append_literal(char* output, const size_t capacity,
                                  size_t& offset,
                                  const char (&literal)[N]) noexcept {
    static_assert(N > 0);
    constexpr size_t kBytes = N - 1U;
    if (!output || kBytes > capacity - offset) return false;
    std::memcpy(output + offset, literal, kBytes);
    offset += kBytes;
    return true;
}

[[nodiscard]] bool append_unsigned(char* output, const size_t capacity,
                                   size_t& offset,
                                   uint64_t value) noexcept {
    char reversed[20]{};
    size_t count = 0;
    do {
        reversed[count++] = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0);
    if (!output || count > capacity - offset) return false;
    while (count != 0) output[offset++] = reversed[--count];
    return true;
}

[[nodiscard]] bool append_signed(char* output, const size_t capacity,
                                 size_t& offset,
                                 const int64_t value) noexcept {
    if (value >= 0) return append_unsigned(
        output, capacity, offset, static_cast<uint64_t>(value));
    if (!output || offset == capacity) return false;
    output[offset++] = '-';
    // Convert through unsigned arithmetic so INT64_MIN is representable.
    const uint64_t magnitude = 0U - static_cast<uint64_t>(value);
    return append_unsigned(output, capacity, offset, magnitude);
}

[[nodiscard]] bool format_fill_json(
    char* output, const size_t capacity,
    const ExecutionGateway::FillEvent& event, size_t& output_size) noexcept {
    output_size = 0;
    return append_literal(output, capacity, output_size,
                          "{\"event\":\"fill\",\"order_id\":") &&
           append_unsigned(output, capacity, output_size, event.order_id) &&
           append_literal(output, capacity, output_size,
                          ",\"filled_qty\":") &&
           append_signed(output, capacity, output_size, event.filled_qty) &&
           append_literal(output, capacity, output_size,
                          ",\"fill_price\":") &&
           append_signed(output, capacity, output_size, event.fill_price) &&
           append_literal(output, capacity, output_size,
                          ",\"timestamp_ns\":") &&
           append_unsigned(output, capacity, output_size, event.timestamp_ns) &&
           append_literal(output, capacity, output_size, "}");
}

[[nodiscard]] int base64_value(const char value) noexcept {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

// RFC 6455 requires a base64 representation of exactly 16 random bytes.  The
// last quartet must therefore be XX== and its unused low bits must be zero.
[[nodiscard]] bool decode_websocket_key(
    const char* key, std::array<uint8_t, kDecodedKeyBytes>& decoded) noexcept {
    if (!key) return false;

    size_t length = 0;
    while (length <= kWebSocketKeyBytes && key[length] != '\0') ++length;
    if (length != kWebSocketKeyBytes || key[22] != '=' || key[23] != '=') {
        return false;
    }

    size_t output = 0;
    for (size_t input = 0; input < 20; input += 4) {
        const int a = base64_value(key[input]);
        const int b = base64_value(key[input + 1]);
        const int c = base64_value(key[input + 2]);
        const int d = base64_value(key[input + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return false;

        decoded[output++] = static_cast<uint8_t>((a << 2) | (b >> 4));
        decoded[output++] = static_cast<uint8_t>((b << 4) | (c >> 2));
        decoded[output++] = static_cast<uint8_t>((c << 6) | d);
    }

    const int a = base64_value(key[20]);
    const int b = base64_value(key[21]);
    if (a < 0 || b < 0 || (b & 0x0F) != 0) return false;
    decoded[output++] = static_cast<uint8_t>((a << 2) | (b >> 4));
    return output == decoded.size();
}

[[nodiscard]] uint32_t rotate_left(const uint32_t value,
                                   const uint32_t bits) noexcept {
    return (value << bits) | (value >> (32U - bits));
}

// The accept input is always the 24-byte validated key plus the 36-byte GUID,
// so two SHA-1 blocks are sufficient. Keeping it local avoids another crypto
// dependency and keeps the handshake allocation-free.
void sha1(const uint8_t* input, const size_t input_size,
          std::array<uint8_t, kSha1Bytes>& digest) noexcept {
    std::array<uint8_t, 128> padded{};
    if (!input || input_size > 119) return;

    std::memcpy(padded.data(), input, input_size);
    padded[input_size] = 0x80U;
    const size_t padded_size = input_size < 56 ? 64 : 128;
    const uint64_t bit_length = static_cast<uint64_t>(input_size) * 8U;
    for (size_t index = 0; index < 8; ++index) {
        padded[padded_size - 1U - index] =
            static_cast<uint8_t>(bit_length >> (index * 8U));
    }

    uint32_t state[5] = {
        0x67452301U,
        0xEFCDAB89U,
        0x98BADCFEU,
        0x10325476U,
        0xC3D2E1F0U,
    };

    for (size_t block = 0; block < padded_size; block += 64) {
        std::array<uint32_t, 80> words{};
        for (size_t index = 0; index < 16; ++index) {
            const size_t offset = block + (index * 4U);
            words[index] =
                (static_cast<uint32_t>(padded[offset]) << 24U) |
                (static_cast<uint32_t>(padded[offset + 1U]) << 16U) |
                (static_cast<uint32_t>(padded[offset + 2U]) << 8U) |
                static_cast<uint32_t>(padded[offset + 3U]);
        }
        for (size_t index = 16; index < words.size(); ++index) {
            words[index] = rotate_left(words[index - 3U] ^ words[index - 8U] ^
                                           words[index - 14U] ^ words[index - 16U],
                                       1U);
        }

        uint32_t a = state[0];
        uint32_t b = state[1];
        uint32_t c = state[2];
        uint32_t d = state[3];
        uint32_t e = state[4];
        for (size_t index = 0; index < words.size(); ++index) {
            uint32_t function = 0;
            uint32_t constant = 0;
            if (index < 20) {
                function = (b & c) | ((~b) & d);
                constant = 0x5A827999U;
            } else if (index < 40) {
                function = b ^ c ^ d;
                constant = 0x6ED9EBA1U;
            } else if (index < 60) {
                function = (b & c) | (b & d) | (c & d);
                constant = 0x8F1BBCDCU;
            } else {
                function = b ^ c ^ d;
                constant = 0xCA62C1D6U;
            }
            const uint32_t next = rotate_left(a, 5U) + function + e +
                                  constant + words[index];
            e = d;
            d = c;
            c = rotate_left(b, 30U);
            b = a;
            a = next;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }

    for (size_t word = 0; word < 5; ++word) {
        for (size_t byte = 0; byte < 4; ++byte) {
            digest[(word * 4U) + byte] = static_cast<uint8_t>(
                state[word] >> (24U - static_cast<uint32_t>(byte * 8U)));
        }
    }
}

void base64_encode(const std::array<uint8_t, kSha1Bytes>& input,
                   std::array<char, kAcceptBytes + 1>& output) noexcept {
    constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t written = 0;
    for (size_t offset = 0; offset < input.size(); offset += 3) {
        const size_t remaining = input.size() - offset;
        const uint32_t triple =
            (static_cast<uint32_t>(input[offset]) << 16U) |
            (remaining > 1 ? static_cast<uint32_t>(input[offset + 1U]) << 8U
                           : 0U) |
            (remaining > 2 ? static_cast<uint32_t>(input[offset + 2U]) : 0U);
        output[written++] = kAlphabet[(triple >> 18U) & 0x3FU];
        output[written++] = kAlphabet[(triple >> 12U) & 0x3FU];
        output[written++] = remaining > 1
                                ? kAlphabet[(triple >> 6U) & 0x3FU]
                                : '=';
        output[written++] = remaining > 2 ? kAlphabet[triple & 0x3FU] : '=';
    }
    output[written] = '\0';
}

[[nodiscard]] bool make_accept(
    const char* key, std::array<char, kAcceptBytes + 1>& accept) noexcept {
    std::array<uint8_t, kDecodedKeyBytes> decoded{};
    if (!decode_websocket_key(key, decoded)) return false;

    constexpr size_t kInputBytes =
        kWebSocketKeyBytes + sizeof(kWebSocketGuid) - 1U;
    std::array<uint8_t, kInputBytes> input{};
    std::memcpy(input.data(), key, kWebSocketKeyBytes);
    std::memcpy(input.data() + kWebSocketKeyBytes, kWebSocketGuid,
                sizeof(kWebSocketGuid) - 1U);
    std::array<uint8_t, kSha1Bytes> digest{};
    sha1(input.data(), input.size(), digest);
    base64_encode(digest, accept);
    return true;
}

[[nodiscard]] bool make_nonblocking(const int fd) noexcept {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

// A pipe is available on every supported POSIX target and integrates with the
// existing poll-based socket worker. It is a notification channel only: the
// SPSC queues remain the ownership boundary for fills and upgrades.
[[nodiscard]] bool create_wakeup_pipe(int& read_fd, int& write_fd) noexcept {
    int descriptors[2]{-1, -1};
    if (::pipe(descriptors) != 0 || !make_nonblocking(descriptors[0]) ||
        !make_nonblocking(descriptors[1])) {
        if (descriptors[0] >= 0) (void)::close(descriptors[0]);
        if (descriptors[1] >= 0) (void)::close(descriptors[1]);
        return false;
    }
    read_fd = descriptors[0];
    write_fd = descriptors[1];
    return true;
}

void close_wakeup_fd(int& descriptor) noexcept {
    if (descriptor >= 0) (void)::close(descriptor);
    descriptor = -1;
}

[[nodiscard]] bool valid_close_code(const uint16_t code) noexcept {
    if (code >= 3000 && code <= 4999) return true;
    return code >= 1000 && code <= 1014 && code != 1004 && code != 1005 &&
           code != 1006;
}

// RFC 6455 §5.5.1 requires a close reason to be valid UTF-8.  Decode only
// enough to reject malformed, overlong, surrogate, and out-of-range forms;
// the payload remains in its fixed connection buffer.
[[nodiscard]] bool valid_utf8(const char* text, const size_t size) noexcept {
    if (size != 0 && !text) return false;
    size_t offset = 0;
    while (offset < size) {
        const uint8_t first = static_cast<uint8_t>(text[offset++]);
        if (first <= 0x7FU) continue;

        uint32_t code_point = 0;
        uint8_t continuation_count = 0;
        uint32_t minimum = 0;
        if ((first & 0xE0U) == 0xC0U) {
            code_point = first & 0x1FU;
            continuation_count = 1;
            minimum = 0x80U;
        } else if ((first & 0xF0U) == 0xE0U) {
            code_point = first & 0x0FU;
            continuation_count = 2;
            minimum = 0x800U;
        } else if ((first & 0xF8U) == 0xF0U) {
            code_point = first & 0x07U;
            continuation_count = 3;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (offset + continuation_count > size) return false;
        for (uint8_t index = 0; index < continuation_count; ++index) {
            const uint8_t next = static_cast<uint8_t>(text[offset++]);
            if ((next & 0xC0U) != 0x80U) return false;
            code_point = (code_point << 6U) | (next & 0x3FU);
        }
        if (code_point < minimum || code_point > 0x10FFFFU ||
            (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            return false;
        }
    }
    return true;
}

}  // namespace

Server::~Server() { stop(); }

Server::Diagnostics Server::diagnostics() const noexcept {
    return {
        .completed_broadcasts =
            completed_broadcasts_.load(std::memory_order_relaxed),
        .last_broadcast_latency_ns =
            last_broadcast_latency_ns_.load(std::memory_order_relaxed),
        .last_fanout_count = last_fanout_count_.load(std::memory_order_relaxed),
        .queued_frames = queued_frames_.load(std::memory_order_relaxed),
        .dropped_frames = dropped_frames_.load(std::memory_order_relaxed),
        .slow_client_closes =
            slow_client_closes_.load(std::memory_order_relaxed),
        .last_backpressure_close_ns =
            last_backpressure_close_ns_.load(std::memory_order_relaxed),
        .pings_sent = pings_sent_.load(std::memory_order_relaxed),
        .pongs_received = pongs_received_.load(std::memory_order_relaxed),
        .last_pong_rtt_ns = last_pong_rtt_ns_.load(std::memory_order_relaxed),
        .cleanup_count = cleanup_count_.load(std::memory_order_relaxed),
    };
}

uint64_t Server::broadcast_latency_sample(
    const uint64_t sequence) const noexcept {
    return broadcast_latency_samples_[sequence & kLatencySampleMask];
}

bool Server::start(const std::span<std::byte> connection_storage) noexcept {
    if (running_.exchange(true, std::memory_order_acq_rel)) return false;
    close_admission(upgrade_admission_);
    close_admission(publish_admission_);

    const size_t required_storage =
        connection_pool_storage_bytes(max_connections_);
    const uintptr_t storage_address = reinterpret_cast<uintptr_t>(
        connection_storage.data());
    if (required_storage == 0 || !connection_storage.data() ||
        connection_storage.size() < required_storage ||
        storage_address % connection_storage_alignment() != 0) {
        Upgrade pending{};
        while (upgrades_.try_pop(pending)) close_pending_upgrade(pending);
        running_.store(false, std::memory_order_release);
        return false;
    }
    // The slab is contiguous by construction; subscription buckets rely on
    // `&connection - connections_` for their allocation-free index.
    connections_ = reinterpret_cast<Connection*>(connection_storage.data());
    connection_storage_ = connection_storage.first(required_storage);
    reset_subscriptions();
    broadcast_latency_samples_.fill(0);
    for (uint32_t index = 0; index < max_connections_; ++index) {
        ::new (static_cast<void*>(connections_ + index)) Connection{};
        ++constructed_connections_;
    }

    // Keep the worker asleep while there is no socket activity. A successful
    // fill/upgrade queue push writes one coalesced byte here so poll() wakes
    // before the producer's next execution turn; no payload is sent through
    // this descriptor.
    if (!create_wakeup_pipe(wake_read_fd_, wake_write_fd_)) {
        for (uint32_t index = 0; index < constructed_connections_; ++index) {
            connections_[index].~Connection();
        }
        constructed_connections_ = 0;
        connections_ = nullptr;
        connection_storage_ = {};
        Upgrade pending{};
        while (upgrades_.try_pop(pending)) close_pending_upgrade(pending);
        running_.store(false, std::memory_order_release);
        return false;
    }
    worker_wakeup_pending_.store(false, std::memory_order_release);
#if defined(__APPLE__)
    event_queue_fd_ = ::kqueue();
    struct kevent wake_change{};
    bool wake_registered = false;
    if (event_queue_fd_ >= 0) {
        EV_SET(&wake_change, static_cast<uintptr_t>(wake_read_fd_),
               EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        wake_registered = ::kevent(event_queue_fd_, &wake_change, 1, nullptr,
                                   0, nullptr) == 0;
    }
    if (!wake_registered) {
        close_wakeup_fd(event_queue_fd_);
        close_wakeup_fd(wake_read_fd_);
        close_wakeup_fd(wake_write_fd_);
        for (uint32_t index = 0; index < constructed_connections_; ++index) {
            connections_[index].~Connection();
        }
        constructed_connections_ = 0;
        connections_ = nullptr;
        connection_storage_ = {};
        Upgrade pending{};
        while (upgrades_.try_pop(pending)) close_pending_upgrade(pending);
        running_.store(false, std::memory_order_release);
        return false;
    }
#endif
    pollfd inactive{};
    inactive.fd = -1;
    poll_fds_.fill(inactive);
    poll_fds_[0].fd = wake_read_fd_;
    poll_fds_[0].events = POLLIN;
    flush_head_ = -1;
    flush_tail_ = -1;
    const uint64_t now = monotonic_now_ns();
    next_maintenance_ns_ = now + kMaintenanceIntervalNs;
    last_fill_activity_ns_ = 0;

    try {
        thread_ = std::thread(&Server::run, this);
    } catch (...) {
        close_wakeup_fd(event_queue_fd_);
        close_wakeup_fd(wake_read_fd_);
        close_wakeup_fd(wake_write_fd_);
        for (uint32_t index = 0; index < constructed_connections_; ++index) {
            connections_[index].~Connection();
        }
        constructed_connections_ = 0;
        connections_ = nullptr;
        connection_storage_ = {};
        Upgrade pending{};
        while (upgrades_.try_pop(pending)) close_pending_upgrade(pending);
        running_.store(false, std::memory_order_release);
        return false;
    }
    // Publish the live fixed pool and worker only after they both exist.
    // CAS-based admission makes this visible as one safe ownership boundary
    // to the HTTP and execution producers.
    open_admission(upgrade_admission_);
    open_admission(publish_admission_);
    return true;
}

void Server::stop() noexcept {
    // Revoke both producer paths before waiting. A successful CAS reference
    // was acquired before this close and is therefore drained below; a CAS
    // racing after this close cannot enter either bounded ring.
    close_admission(upgrade_admission_);
    close_admission(publish_admission_);
    wait_for_admission_drain(upgrade_admission_);
    wait_for_admission_drain(publish_admission_);
    running_.store(false, std::memory_order_release);
    // The worker can be blocked in poll() for its next ping deadline. No
    // producer lease remains at this point, so force a final wake even if a
    // previous producer left the coalescing bit set.
    worker_wakeup_pending_.store(false, std::memory_order_release);
    signal_worker();
    if (thread_.joinable()) thread_.join();

    if (connections_) {
        for (uint32_t index = 0; index < max_connections_; ++index) {
            close(connections_[index]);
        }
    }

    Upgrade pending{};
    while (upgrades_.try_pop(pending)) close_pending_upgrade(pending);
    PendingFill fill{};
    while (events_.try_pop(fill)) {
    }

    if (connections_) {
        for (uint32_t index = 0; index < constructed_connections_; ++index) {
            connections_[index].~Connection();
        }
        constructed_connections_ = 0;
        connections_ = nullptr;
        connection_storage_ = {};
    }
    close_wakeup_fd(wake_read_fd_);
    close_wakeup_fd(wake_write_fd_);
    close_wakeup_fd(event_queue_fd_);
    worker_wakeup_pending_.store(false, std::memory_order_release);
    pollfd inactive{};
    inactive.fd = -1;
    poll_fds_.fill(inactive);
    next_maintenance_ns_ = 0;
    last_fill_activity_ns_ = 0;
    flush_head_ = -1;
    flush_tail_ = -1;
    active_.store(0, std::memory_order_release);
    reserved_.store(0, std::memory_order_release);
}

void Server::close_admission(std::atomic<uint32_t>& admission) noexcept {
    (void)admission.fetch_or(kAdmissionClosedBit, std::memory_order_acq_rel);
}

void Server::open_admission(std::atomic<uint32_t>& admission) noexcept {
    // stop() drains every accepted reference before a future start() opens
    // the source again. Resetting the word also clears the closed bit.
    admission.store(0, std::memory_order_release);
}

void Server::wait_for_admission_drain(
    const std::atomic<uint32_t>& admission) noexcept {
    while ((admission.load(std::memory_order_acquire) & kAdmissionRefMask) != 0) {
        std::this_thread::yield();
    }
}

bool Server::acquire_admission(std::atomic<uint32_t>& admission) noexcept {
    uint32_t observed = admission.load(std::memory_order_acquire);
    for (;;) {
        if ((observed & kAdmissionClosedBit) != 0 ||
            (observed & kAdmissionRefMask) == kAdmissionRefMask) {
            return false;
        }
        const uint32_t desired = observed + 1U;
        if (admission.compare_exchange_weak(
                observed, desired, std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return true;
        }
    }
}

void Server::release_admission(std::atomic<uint32_t>& admission) noexcept {
    (void)admission.fetch_sub(1U, std::memory_order_release);
}

bool Server::acquire_upgrade_lease() noexcept {
    return acquire_admission(upgrade_admission_);
}

void Server::release_upgrade_lease() noexcept {
    release_admission(upgrade_admission_);
}

bool Server::acquire_publish_lease() noexcept {
    return acquire_admission(publish_admission_);
}

void Server::release_publish_lease() noexcept {
    release_admission(publish_admission_);
}

void Server::signal_worker() noexcept {
    const int descriptor = wake_write_fd_;
    if (descriptor < 0) return;
    worker_wakeup_pending_.store(true, std::memory_order_release);

    const uint8_t signal = 1U;
    for (;;) {
        const ssize_t written = ::write(descriptor, &signal, sizeof(signal));
        if (written == static_cast<ssize_t>(sizeof(signal))) return;
        if (written < 0 && errno == EINTR) continue;
        // EAGAIN means the bounded pipe already contains an unread wakeup;
        // no additional notification is needed. Stop() closes admission
        // before closing this descriptor, so other errors are non-fatal.
        return;
    }
}

void Server::drain_worker_signal() noexcept {
    if (wake_read_fd_ < 0) return;
    std::array<uint8_t, 64> signals{};
    for (;;) {
        const ssize_t read = ::read(wake_read_fd_, signals.data(),
                                    signals.size());
        if (read > 0) continue;
        if (read < 0 && errno == EINTR) continue;
        return;
    }
}

void Server::refresh_poll_interest(Connection& connection) noexcept {
#if defined(__APPLE__)
    // kqueue owns readiness registration on macOS. Fresh output is placed on
    // the worker's fixed flush list and only an EAGAIN path arms EVFILT_WRITE.
    (void)connection;
#else
    if (!connections_ || &connection < connections_ ||
        &connection >= connections_ + max_connections_) {
        return;
    }
    const size_t index = static_cast<size_t>(&connection - connections_) + 1U;
    pollfd& descriptor = poll_fds_[index];
    descriptor.fd = connection.fd;
    descriptor.events = connection.fd < 0 ? 0 : POLLIN;
    if (connection.fd >= 0 &&
        connection.write_offset != connection.write_size) {
        descriptor.events = static_cast<short>(descriptor.events | POLLOUT);
    }
    descriptor.revents = 0;
#endif
}

void Server::clear_poll_interest(Connection& connection) noexcept {
    if (!connections_ || &connection < connections_ ||
        &connection >= connections_ + max_connections_) {
        return;
    }
#if defined(__APPLE__)
    if (event_queue_fd_ >= 0 && connection.fd >= 0) {
        std::array<struct kevent, 2> changes{};
        EV_SET(&changes[0], static_cast<uintptr_t>(connection.fd),
               EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        int change_count = 1;
        if (connection.write_waiting) {
            EV_SET(&changes[1], static_cast<uintptr_t>(connection.fd),
                   EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
            change_count = 2;
        }
        (void)::kevent(event_queue_fd_, changes.data(), change_count, nullptr,
                       0, nullptr);
    }
    connection.write_waiting = false;
#else
    const size_t index = static_cast<size_t>(&connection - connections_) + 1U;
    poll_fds_[index] = {};
    poll_fds_[index].fd = -1;
#endif
}

bool Server::register_connection_io(Connection& connection) noexcept {
    if (!connections_ || connection.fd < 0 || &connection < connections_ ||
        &connection >= connections_ + max_connections_) {
        return false;
    }
#if defined(__APPLE__)
    if (event_queue_fd_ < 0) return false;
    struct kevent change{};
    EV_SET(&change, static_cast<uintptr_t>(connection.fd), EVFILT_READ,
           EV_ADD | EV_ENABLE, 0, 0, &connection);
    return ::kevent(event_queue_fd_, &change, 1, nullptr, 0, nullptr) == 0;
#else
    refresh_poll_interest(connection);
    return true;
#endif
}

bool Server::set_write_interest(Connection& connection,
                                const bool enabled) noexcept {
    if (!connections_ || connection.fd < 0 || &connection < connections_ ||
        &connection >= connections_ + max_connections_) {
        return false;
    }
    if (connection.write_waiting == enabled) return true;
#if defined(__APPLE__)
    if (event_queue_fd_ < 0) return false;
    struct kevent change{};
    EV_SET(&change, static_cast<uintptr_t>(connection.fd), EVFILT_WRITE,
           enabled ? EV_ADD | EV_ENABLE : EV_DISABLE, 0, 0, &connection);
    if (::kevent(event_queue_fd_, &change, 1, nullptr, 0, nullptr) != 0) {
        return false;
    }
#else
    refresh_poll_interest(connection);
#endif
    connection.write_waiting = enabled;
    return true;
}

void Server::schedule_flush(Connection& connection) noexcept {
#if defined(__APPLE__)
    if (!connections_ || connection.fd < 0 || connection.flush_queued ||
        &connection < connections_ || connection >= connections_ + max_connections_) {
        return;
    }
    const int32_t index = static_cast<int32_t>(&connection - connections_);
    connection.flush_queued = true;
    connection.flush_next = -1;
    if (flush_tail_ >= 0) {
        connections_[static_cast<uint32_t>(flush_tail_)].flush_next = index;
    } else {
        flush_head_ = index;
    }
    flush_tail_ = index;
#else
    (void)connection;
#endif
}

void Server::flush_queued(const uint64_t timestamp_ns) noexcept {
#if defined(__APPLE__)
    int32_t index = flush_head_;
    flush_head_ = -1;
    flush_tail_ = -1;
    while (index >= 0) {
        Connection& connection = connections_[static_cast<uint32_t>(index)];
        const int32_t next = connection.flush_next;
        connection.flush_next = -1;
        connection.flush_queued = false;
        if (connection.fd >= 0 &&
            connection.write_offset != connection.write_size) {
            (void)flush(connection, timestamp_ns);
        }
        index = next;
    }
#else
    (void)timestamp_ns;
#endif
}

UpgradeResult Server::enqueue_upgrade(const int fd, const uint64_t order_id,
                                      const char* key, const char* pre_read,
                                      const size_t pre_read_size) noexcept {
    std::array<uint8_t, kDecodedKeyBytes> decoded{};
    if (fd < 0 || !decode_websocket_key(key, decoded)) {
        if (fd >= 0) (void)::close(fd);
        return UpgradeResult::kInvalidKey;
    }
    if (pre_read_size > kMaxUpgradePreReadBytes ||
        (pre_read_size != 0 && !pre_read)) {
        (void)::close(fd);
        return UpgradeResult::kInvalidUpgrade;
    }
    if (!acquire_upgrade_lease()) {
        (void)::close(fd);
        return UpgradeResult::kQueueFull;
    }

    uint32_t reserved = reserved_.load(std::memory_order_acquire);
    do {
        if (reserved >= max_connections_) {
            release_upgrade_lease();
            (void)::close(fd);
            return UpgradeResult::kPoolExhausted;
        }
    } while (!reserved_.compare_exchange_weak(
        reserved, reserved + 1U, std::memory_order_acq_rel,
        std::memory_order_acquire));

    Upgrade upgrade{};
    upgrade.fd = fd;
    upgrade.order_id = order_id;
    std::memcpy(upgrade.key, key, kWebSocketKeyBytes);
    upgrade.key[kWebSocketKeyBytes] = '\0';
    upgrade.pre_read_size = static_cast<uint16_t>(pre_read_size);
    if (pre_read_size != 0) {
        std::memcpy(upgrade.pre_read.data(), pre_read, pre_read_size);
    }
    if (!upgrades_.try_push(upgrade)) {
        reserved_.fetch_sub(1U, std::memory_order_acq_rel);
        release_upgrade_lease();
        (void)::close(fd);
        return UpgradeResult::kQueueFull;
    }
    signal_worker();
    release_upgrade_lease();
    return UpgradeResult::kAccepted;
}

bool Server::publish(const ExecutionGateway::FillEvent& event) noexcept {
    // The execution callback is normally detached before shutdown, but this
    // lease makes the standalone server lifecycle safe too. Keep every
    // member access before release: stop() may unmap immediately afterward.
    if (!acquire_publish_lease()) return false;
    const PendingFill pending{.event = event, .published_ns = monotonic_now_ns()};
    const bool queued = events_.try_push(pending);
    if (!queued) {
        dropped_.fetch_add(1U, std::memory_order_relaxed);
    } else {
        signal_worker();
    }
    release_publish_lease();
    return queued;
}

bool Server::publish_callback(void* context,
                              const ExecutionGateway::FillEvent& event) noexcept {
    return context && static_cast<Server*>(context)->publish(event);
}

void Server::close_pending_upgrade(const Upgrade& upgrade) noexcept {
    if (upgrade.fd >= 0) (void)::close(upgrade.fd);
    reserved_.fetch_sub(1U, std::memory_order_acq_rel);
}

void Server::reset_subscriptions() noexcept {
    for (SubscriptionBucket& bucket : subscriptions_) {
        bucket = SubscriptionBucket{};
    }
}

uint32_t Server::subscription_hash(const uint64_t order_id) noexcept {
    // SplitMix-style avalanche makes sequential order IDs distribute across
    // the fixed power-of-two table without allocating a map per subscriber.
    uint64_t value = order_id + 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return static_cast<uint32_t>((value ^ (value >> 31U)) &
                                 kSubscriptionMask);
}

int32_t Server::find_subscription(const uint64_t order_id) const noexcept {
    uint32_t index = subscription_hash(order_id);
    for (uint32_t probe = 0; probe < kSubscriptionBuckets; ++probe) {
        const SubscriptionBucket& bucket = subscriptions_[index];
        if (bucket.state == SubscriptionBucket::State::kEmpty) return -1;
        if (bucket.state == SubscriptionBucket::State::kOccupied &&
            bucket.order_id == order_id) {
            return static_cast<int32_t>(index);
        }
        index = (index + 1U) & kSubscriptionMask;
    }
    return -1;
}

int32_t Server::find_or_create_subscription(
    const uint64_t order_id) noexcept {
    uint32_t index = subscription_hash(order_id);
    uint32_t first_tombstone = kSubscriptionBuckets;
    for (uint32_t probe = 0; probe < kSubscriptionBuckets; ++probe) {
        SubscriptionBucket& bucket = subscriptions_[index];
        if (bucket.state == SubscriptionBucket::State::kEmpty) {
            const uint32_t target = first_tombstone == kSubscriptionBuckets
                ? index : first_tombstone;
            SubscriptionBucket& inserted = subscriptions_[target];
            inserted = SubscriptionBucket{};
            inserted.order_id = order_id;
            inserted.state = SubscriptionBucket::State::kOccupied;
            return static_cast<int32_t>(target);
        }
        if (bucket.state == SubscriptionBucket::State::kTombstone) {
            if (first_tombstone == kSubscriptionBuckets) {
                first_tombstone = index;
            }
        } else if (bucket.order_id == order_id) {
            return static_cast<int32_t>(index);
        }
        index = (index + 1U) & kSubscriptionMask;
    }
    if (first_tombstone != kSubscriptionBuckets) {
        SubscriptionBucket& inserted = subscriptions_[first_tombstone];
        inserted = SubscriptionBucket{};
        inserted.order_id = order_id;
        inserted.state = SubscriptionBucket::State::kOccupied;
        return static_cast<int32_t>(first_tombstone);
    }
    return -1;
}

bool Server::subscribe(Connection& connection) noexcept {
    if (!connections_ || connection.fd < 0 ||
        &connection < connections_ ||
        &connection >= connections_ + max_connections_) {
        return false;
    }
    const int32_t bucket_index = find_or_create_subscription(connection.order_id);
    if (bucket_index < 0 ||
        static_cast<uint32_t>(bucket_index) >= kSubscriptionBuckets) {
        return false;
    }
    SubscriptionBucket& bucket =
        subscriptions_[static_cast<uint32_t>(bucket_index)];
    const int32_t connection_index = static_cast<int32_t>(&connection - connections_);
    connection.subscription_bucket = static_cast<uint16_t>(bucket_index);
    connection.subscription_prev = -1;
    connection.subscription_next = bucket.head;
    if (bucket.head >= 0) {
        connections_[static_cast<uint32_t>(bucket.head)].subscription_prev =
            connection_index;
    }
    bucket.head = connection_index;
    return true;
}

void Server::unsubscribe(Connection& connection) noexcept {
    if (!connections_ || connection.subscription_bucket == kNoSubscriptionBucket ||
        connection.subscription_bucket >= kSubscriptionBuckets ||
        &connection < connections_ ||
        &connection >= connections_ + max_connections_) {
        return;
    }
    const uint32_t bucket_index = connection.subscription_bucket;
    SubscriptionBucket& bucket = subscriptions_[bucket_index];
    const int32_t connection_index = static_cast<int32_t>(&connection - connections_);
    const int32_t previous = connection.subscription_prev;
    const int32_t next = connection.subscription_next;
    if (previous >= 0) {
        connections_[static_cast<uint32_t>(previous)].subscription_next = next;
    } else if (bucket.head == connection_index) {
        bucket.head = next;
    }
    if (next >= 0) {
        connections_[static_cast<uint32_t>(next)].subscription_prev = previous;
    }
    if (bucket.head < 0) {
        bucket.head = -1;
        bucket.state = SubscriptionBucket::State::kTombstone;
    }
    connection.subscription_bucket = kNoSubscriptionBucket;
    connection.subscription_prev = -1;
    connection.subscription_next = -1;
}

void Server::close(Connection& connection) noexcept {
    if (connection.fd < 0) return;
    unsubscribe(connection);
    clear_poll_interest(connection);
    (void)::close(connection.fd);
    connection = Connection{};
    active_.fetch_sub(1U, std::memory_order_acq_rel);
    reserved_.fetch_sub(1U, std::memory_order_acq_rel);
    cleanup_count_.fetch_add(1U, std::memory_order_relaxed);
}

bool Server::queue_bytes(Connection& connection, const void* bytes,
                         const size_t count) noexcept {
    if (count == 0) return true;
    if (!bytes || count > kWriteBufferBytes) return false;

    const size_t pending = static_cast<size_t>(connection.write_size) -
                           static_cast<size_t>(connection.write_offset);
    if (pending + count > kWriteBufferBytes) return false;
    // `write_blocked_since_ns` is armed only after send() establishes that
    // the socket cannot make progress (EAGAIN), or after a partial write.
    // Queuing bytes is deliberately syscall-free: a connection with pending
    // output is not slow until a nonblocking write says so.  This lets one
    // fill fanout enqueue to many healthy subscribers without issuing a send
    // syscall (or a clock read) per subscriber.
    if (connection.write_offset != 0 && pending != 0) {
        std::memmove(connection.write_buffer.data(),
                     connection.write_buffer.data() + connection.write_offset,
                     pending);
    }
    connection.write_offset = 0;
    connection.write_size = static_cast<uint16_t>(pending);
    std::memcpy(connection.write_buffer.data() + connection.write_size, bytes,
                count);
    connection.write_size = static_cast<uint16_t>(pending + count);
    refresh_poll_interest(connection);
    return true;
}

bool Server::queue_frame(Connection& connection, const uint8_t opcode,
                         const void* payload,
                         const size_t payload_size) noexcept {
    if ((opcode & 0xF0U) != 0 || (payload_size != 0 && !payload) ||
        payload_size > 0xFFFFU) {
        return false;
    }

    std::array<uint8_t, 4> header{};
    size_t header_size = 2;
    header[0] = static_cast<uint8_t>(0x80U | opcode);  // FIN, no RSV bits.
    if (payload_size <= 125) {
        header[1] = static_cast<uint8_t>(payload_size);  // Server frames unmasked.
    } else {
        header[1] = 126U;
        header[2] = static_cast<uint8_t>(payload_size >> 8U);
        header[3] = static_cast<uint8_t>(payload_size);
        header_size = 4;
    }

    const size_t pending = static_cast<size_t>(connection.write_size) -
                           static_cast<size_t>(connection.write_offset);
    if (pending + header_size + payload_size > kWriteBufferBytes) return false;
    return queue_bytes(connection, header.data(), header_size) &&
           queue_bytes(connection, payload, payload_size);
}

bool Server::queue_close(Connection& connection, const uint16_t code,
                         const void* reason, const size_t reason_size) noexcept {
    if (reason_size > 123 || (reason_size != 0 && !reason)) return false;
    std::array<uint8_t, 125> payload{};
    payload[0] = static_cast<uint8_t>(code >> 8U);
    payload[1] = static_cast<uint8_t>(code);
    if (reason_size != 0) std::memcpy(payload.data() + 2, reason, reason_size);
    return queue_frame(connection, 0x08U, payload.data(), reason_size + 2U);
}

bool Server::close_with_code(Connection& connection, const uint16_t code) noexcept {
    if (!queue_close(connection, code, nullptr, 0)) {
        close(connection);
        return false;
    }
    connection.close_after_flush = true;
    return true;
}

bool Server::flush(Connection& connection, const uint64_t timestamp_ns) noexcept {
    if (connection.write_offset == connection.write_size) {
        connection.write_offset = 0;
        connection.write_size = 0;
        connection.write_blocked_since_ns = 0;
        if (connection.close_after_flush) {
            close(connection);
            return false;
        }
        refresh_poll_interest(connection);
        return true;
    }

    const size_t pending = static_cast<size_t>(connection.write_size) -
                           static_cast<size_t>(connection.write_offset);
    const ssize_t sent = ::send(connection.fd,
                                connection.write_buffer.data() +
                                    connection.write_offset,
                                pending, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent > 0) {
        connection.write_offset = static_cast<uint16_t>(
            static_cast<size_t>(connection.write_offset) +
            static_cast<size_t>(sent));
        if (connection.write_offset == connection.write_size) {
            connection.write_offset = 0;
            connection.write_size = 0;
            connection.write_blocked_since_ns = 0;
            if (connection.close_after_flush) {
                close(connection);
                return false;
            }
        } else {
            // A partial write is valid.  Bound only the period with no
            // further progress rather than treating it as an immediate error.
            connection.write_blocked_since_ns = timestamp_ns;
        }
        refresh_poll_interest(connection);
        return true;
    }

    if (sent < 0 && errno == EINTR) return true;
    if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        close(connection);
        return false;
    }

    if (connection.write_blocked_since_ns == 0) {
        connection.write_blocked_since_ns = timestamp_ns;
        return true;
    }
    if (timestamp_ns - connection.write_blocked_since_ns >=
        kBackpressureCloseNs) {
        last_backpressure_close_ns_.store(
            timestamp_ns - connection.write_blocked_since_ns,
            std::memory_order_relaxed);
        slow_client_closes_.fetch_add(1U, std::memory_order_relaxed);
        close(connection);
        return false;
    }
    return true;
}

Server::FrameResult Server::consume_frame(Connection& connection,
                                          const uint64_t timestamp_ns) noexcept {
    // RFC 6455 inbound layout:
    //   byte 0: FIN | RSV1..3 | opcode
    //   byte 1: MASK | payload length selector
    //   optional 16/64-bit length, then mandatory client mask key, payload.
    // Client payload bytes are unmasked in-place before control/data handling.
    if (connection.read_size < 2) return FrameResult::kIncomplete;

    const auto* input = reinterpret_cast<const uint8_t*>(
        connection.read_buffer.data());
    const uint8_t first = input[0];
    const uint8_t second = input[1];
    const bool final = (first & 0x80U) != 0;
    const uint8_t opcode = first & 0x0FU;
    const bool masked = (second & 0x80U) != 0;
    const uint8_t length_marker = second & 0x7FU;
    if ((first & 0x70U) != 0 || !masked) return FrameResult::kProtocolError;

    size_t header_size = 2;
    uint64_t payload_size_64 = 0;
    if (length_marker <= 125) {
        payload_size_64 = length_marker;
    } else if (length_marker == 126) {
        if (connection.read_size < 4) return FrameResult::kIncomplete;
        payload_size_64 =
            (static_cast<uint64_t>(input[2]) << 8U) | input[3];
        if (payload_size_64 < 126) return FrameResult::kProtocolError;
        header_size = 4;
    } else {
        if (connection.read_size < 10) return FrameResult::kIncomplete;
        if ((input[2] & 0x80U) != 0) return FrameResult::kProtocolError;
        for (size_t index = 0; index < 8; ++index) {
            payload_size_64 = (payload_size_64 << 8U) | input[2U + index];
        }
        if (payload_size_64 <= 0xFFFFU) return FrameResult::kProtocolError;
        header_size = 10;
    }

    const bool control = (opcode & 0x08U) != 0;
    if (control && (!final || payload_size_64 > 125)) {
        return FrameResult::kProtocolError;
    }
    if (payload_size_64 >
        static_cast<uint64_t>(kReadBufferBytes - kMaxClientFrameHeaderBytes)) {
        return FrameResult::kMessageTooLarge;
    }

    const size_t payload_size = static_cast<size_t>(payload_size_64);
    const size_t frame_size = header_size + 4U + payload_size;
    if (connection.read_size < frame_size) return FrameResult::kIncomplete;

    const uint8_t* mask = input + header_size;
    char* payload = connection.read_buffer.data() + header_size + 4U;
    for (size_t index = 0; index < payload_size; ++index) {
        payload[index] = static_cast<char>(
            static_cast<uint8_t>(payload[index]) ^ mask[index & 0x03U]);
    }

    if (control) {
        switch (opcode) {
        case 0x08U: {
            if (payload_size == 1) return FrameResult::kProtocolError;
            if (payload_size >= 2) {
                const uint16_t code = static_cast<uint16_t>(
                    (static_cast<uint16_t>(
                         static_cast<uint8_t>(payload[0])) << 8U) |
                    static_cast<uint8_t>(payload[1]));
                if (!valid_close_code(code)) return FrameResult::kProtocolError;
            }
            if (payload_size > 2 &&
                !valid_utf8(payload + 2, payload_size - 2)) {
                return FrameResult::kInvalidUtf8;
            }
            // Echo a valid peer close payload, including normal-closure 1000.
            if (!queue_frame(connection, 0x08U, payload, payload_size)) {
                slow_client_closes_.fetch_add(1U, std::memory_order_relaxed);
                close(connection);
                return FrameResult::kConsumed;
            }
            connection.close_after_flush = true;
            break;
        }
        case 0x09U:  // Ping: reply with the same application payload.
            if (!queue_frame(connection, 0x0AU, payload, payload_size)) {
                slow_client_closes_.fetch_add(1U, std::memory_order_relaxed);
                close(connection);
                return FrameResult::kConsumed;
            }
            break;
        case 0x0AU:  // Pong: never dispatch this as application data.
            connection.last_pong_ns = timestamp_ns;
            pongs_received_.fetch_add(1U, std::memory_order_relaxed);
            if (connection.awaiting_pong && timestamp_ns >= connection.last_ping_ns) {
                last_pong_rtt_ns_.store(timestamp_ns - connection.last_ping_ns,
                                        std::memory_order_relaxed);
                connection.awaiting_pong = false;
            }
            break;
        default:
            // A well-framed, unknown control opcode is discarded. This keeps
            // extensions from crashing a client session while RSV bits remain
            // strictly rejected above.
            break;
        }
    } else {
        // Data frames are fully unmasked before reaching this branch. The
        // simulator currently has no client-to-server WS application command,
        // but it still enforces RFC 6455's 0/1/2 fragmentation state machine
        // before discarding application payloads.
        if (opcode == 0x00U) {
            if (!connection.fragmented_data) return FrameResult::kProtocolError;
            if (final) connection.fragmented_data = false;
        } else if (opcode == 0x01U || opcode == 0x02U) {
            if (connection.fragmented_data) return FrameResult::kProtocolError;
            connection.fragmented_data = !final;
        } else {
            return FrameResult::kProtocolError;
        }
    }

    const size_t remaining = static_cast<size_t>(connection.read_size) - frame_size;
    if (remaining != 0) {
        std::memmove(connection.read_buffer.data(),
                     connection.read_buffer.data() + frame_size, remaining);
    }
    connection.read_size = static_cast<uint16_t>(remaining);
    return FrameResult::kConsumed;
}

bool Server::receive(Connection& connection,
                     const uint64_t timestamp_ns) noexcept {
    for (uint32_t pass = 0; pass < 8; ++pass) {
        for (;;) {
            const FrameResult result = consume_frame(connection, timestamp_ns);
            if (result == FrameResult::kConsumed) {
                if (connection.fd < 0 || connection.close_after_flush) return true;
                continue;
            }
            if (result == FrameResult::kProtocolError) {
                return close_with_code(connection, 1002);
            }
            if (result == FrameResult::kInvalidUtf8) {
                return close_with_code(connection, 1007);
            }
            if (result == FrameResult::kMessageTooLarge) {
                return close_with_code(connection, 1009);
            }
            break;
        }

        if (connection.close_after_flush) return true;
        if (connection.read_size == kReadBufferBytes) {
            return close_with_code(connection, 1009);
        }

        const size_t remaining = kReadBufferBytes - connection.read_size;
        const ssize_t received =
            ::recv(connection.fd,
                   connection.read_buffer.data() + connection.read_size,
                   remaining, MSG_DONTWAIT);
        if (received > 0) {
            connection.read_size = static_cast<uint16_t>(
                static_cast<size_t>(connection.read_size) +
                static_cast<size_t>(received));
            continue;
        }
        if (received == 0) {
            close(connection);
            return false;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        close(connection);
        return false;
    }
    return true;
}

void Server::broadcast(const PendingFill& fill) noexcept {
    char payload[256]{};
    size_t payload_size = 0;
    const bool formatted = format_fill_json(
        payload, sizeof(payload), fill.event, payload_size);
    uint32_t fanout = 0;
    if (formatted && payload_size != 0 && connections_) {
        const int32_t bucket_index = find_subscription(fill.event.order_id);
        int32_t connection_index = bucket_index < 0
            ? -1 : subscriptions_[static_cast<uint32_t>(bucket_index)].head;
        // Walk only this order's intrusive subscriber chain. The old flat
        // 1,000-slot scan made a one-subscriber fill dependent on unrelated
        // connections and could not substantiate the 100 us queueing SLA at
        // configured pool capacity.
        while (connection_index >= 0) {
            Connection& connection =
                connections_[static_cast<uint32_t>(connection_index)];
            // close() unlinks the current node, so retain its successor
            // before any fail-closed backpressure cleanup.
            const int32_t next_connection = connection.subscription_next;
            if (connection.fd < 0 || connection.close_after_flush ||
                connection.order_id != fill.event.order_id) {
                connection_index = next_connection;
                continue;
            }
            // Queueing is intentionally transport-free.  After the worker
            // drains the current fill batch it polls writable sockets and
            // flushes them there, which can coalesce multiple fill frames in
            // a single send().  Calling flush() here would issue one syscall
            // for every healthy subscriber and put that cost on the
            // callback-to-queued latency path.
            const bool queued = queue_frame(connection, 0x01U, payload,
                                            payload_size);
            if (queued) {
                ++fanout;
                queued_frames_.fetch_add(1U, std::memory_order_relaxed);
            } else {
                // The fixed outbound slot cannot hold a complete next frame.
                // Do not silently drop a fill or block the execution path:
                // fail closed and release this subscriber.  If poll/send has
                // already observed EAGAIN, preserve its measured no-progress
                // interval; otherwise this is immediate bounded-buffer
                // exhaustion and the recorded interval is zero.
                dropped_frames_.fetch_add(1U, std::memory_order_relaxed);
                if (connection.fd >= 0) {
                    const uint64_t closed_at = monotonic_now_ns();
                    const uint64_t pending_since =
                        connection.write_blocked_since_ns;
                    last_backpressure_close_ns_.store(
                        pending_since != 0 && closed_at >= pending_since
                            ? closed_at - pending_since
                            : 0,
                        std::memory_order_relaxed);
                    slow_client_closes_.fetch_add(1U, std::memory_order_relaxed);
                    close(connection);
                }
            }
            connection_index = next_connection;
        }
    }

    const uint64_t completed_ns = monotonic_now_ns();
    const uint64_t latency = completed_ns >= fill.published_ns
                                 ? completed_ns - fill.published_ns
                                 : 0;
    last_fanout_count_.store(fanout, std::memory_order_release);
    last_broadcast_latency_ns_.store(latency, std::memory_order_release);
    const uint64_t completed =
        completed_broadcasts_.load(std::memory_order_relaxed);
    broadcast_latency_samples_[completed & kLatencySampleMask] = latency;
    completed_broadcasts_.store(completed + 1U, std::memory_order_release);
}

void Server::service_keepalive(Connection& connection,
                               const uint64_t timestamp_ns) noexcept {
    if (connection.fd < 0) return;

    if (connection.write_offset != connection.write_size &&
        connection.write_blocked_since_ns != 0 &&
        timestamp_ns - connection.write_blocked_since_ns >=
            kBackpressureCloseNs) {
        last_backpressure_close_ns_.store(
            timestamp_ns - connection.write_blocked_since_ns,
            std::memory_order_relaxed);
        slow_client_closes_.fetch_add(1U, std::memory_order_relaxed);
        close(connection);
        return;
    }

    if (connection.close_after_flush) return;

    if (connection.awaiting_pong) {
        if (timestamp_ns - connection.last_ping_ns >= pong_timeout_ns_) {
            close(connection);
        }
        return;
    }
    if (ping_interval_ns_ == 0 ||
        timestamp_ns - connection.last_ping_ns < ping_interval_ns_) {
        return;
    }
    if (!queue_frame(connection, 0x09U, nullptr, 0)) {
        slow_client_closes_.fetch_add(1U, std::memory_order_relaxed);
        close(connection);
        return;
    }
    connection.last_ping_ns = timestamp_ns;
    connection.awaiting_pong = true;
    pings_sent_.fetch_add(1U, std::memory_order_relaxed);
}

void Server::accept_upgrade(const Upgrade& upgrade) noexcept {
    if (!connections_ || upgrade.fd < 0) {
        close_pending_upgrade(upgrade);
        return;
    }

    Connection* slot = nullptr;
    for (uint32_t index = 0; index < max_connections_; ++index) {
        if (connections_[index].fd < 0) {
            slot = connections_ + index;
            break;
        }
    }
    if (!slot) {
        close_pending_upgrade(upgrade);
        return;
    }

    std::array<char, kAcceptBytes + 1> accept{};
    if (!make_accept(upgrade.key, accept) || !make_nonblocking(upgrade.fd)) {
        close_pending_upgrade(upgrade);
        return;
    }

    Connection& connection = *slot;
    connection = Connection{};
    connection.fd = upgrade.fd;
    connection.order_id = upgrade.order_id;
    connection.last_ping_ns = monotonic_now_ns();
    connection.last_pong_ns = connection.last_ping_ns;
    if (upgrade.pre_read_size != 0) {
        std::memcpy(connection.read_buffer.data(), upgrade.pre_read.data(),
                    upgrade.pre_read_size);
        connection.read_size = upgrade.pre_read_size;
    }
    active_.fetch_add(1U, std::memory_order_release);
    if (!subscribe(connection)) {
        // The fixed index is deliberately fail-closed. It should have room
        // for every active slot, but a corrupt/full index must never fall
        // back to an O(max_connections) broadcast scan.
        close(connection);
        return;
    }

    char response[192]{};
    const int written = std::snprintf(
        response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept.data());
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(response) ||
        !queue_bytes(connection, response, static_cast<size_t>(written))) {
        close(connection);
        return;
    }

    // HTTP may already have received one or more complete masked frames in
    // the same TCP read as the upgrade request. Parse those retained bytes
    // now; waiting for POLLIN would otherwise strand them indefinitely.
    if (connection.read_size != 0) {
        (void)receive(connection, monotonic_now_ns());
    }
}

void Server::run() noexcept {
    while (running_.load(std::memory_order_acquire)) {
        // Drain the notification before opening its coalescing gate. If a
        // producer raced just before that store it is already visible in the
        // SPSC queue; if it races after it, it writes a fresh byte that wakes
        // the poll below. This order avoids a lost wake between try_pop() and
        // the blocking poll while keeping the execution callback lock-free.
        drain_worker_signal();
        worker_wakeup_pending_.store(false, std::memory_order_release);

        // Fill fanout is latency-critical; service it ahead of new upgrades.
        // The bounded batch remains fair to socket I/O and upgrade admission.
        PendingFill fill{};
        uint32_t fills_processed = 0;
        for (uint32_t processed = 0;
             processed < kMaxFillsPerTurn && events_.try_pop(fill);
             ++processed) {
            broadcast(fill);
            ++fills_processed;
        }
        if (fills_processed != 0) last_fill_activity_ns_ = monotonic_now_ns();

        Upgrade upgrade{};
        for (uint32_t processed = 0;
             processed < kMaxUpgradesPerTurn && upgrades_.try_pop(upgrade);
             ++processed) {
            accept_upgrade(upgrade);
        }

        // If a bounded fill batch left work behind, make one nonblocking I/O
        // pass before the next batch. This prevents sustained fills from
        // starving flush/receive/close processing while preserving a bounded
        // callback-to-queued interval for the already-consumed batch.
        const bool queue_work_remains = !events_.empty() || !upgrades_.empty();
        const uint64_t now = monotonic_now_ns();
        const bool recently_filled = last_fill_activity_ns_ != 0 &&
            now >= last_fill_activity_ns_ &&
            now - last_fill_activity_ns_ < kPostFillQuietPeriodNs;
        const bool maintenance_due = now >= next_maintenance_ns_;
        const bool maintenance_must_run = maintenance_due &&
            (!recently_filled ||
             now - next_maintenance_ns_ >= kMaximumMaintenanceDeferralNs);
        if (maintenance_must_run && connections_) {
            for (uint32_t index = 0; index < max_connections_; ++index) {
                service_keepalive(connections_[index], now);
            }
            next_maintenance_ns_ = now > UINT64_MAX - kMaintenanceIntervalNs
                ? UINT64_MAX : now + kMaintenanceIntervalNs;
        }

        // `poll_fds_` is fixed and worker-owned: connection slot n always
        // maps to descriptor n+1, inactive entries are fd=-1. A producer wake
        // can therefore return directly to fill fanout without rebuilding or
        // scanning the 1,000-slot poll set first.
        int timeout_ms = 0;
        if (!queue_work_remains) {
            uint64_t deadline = next_maintenance_ns_;
            if (recently_filled) {
                const uint64_t quiet_deadline =
                    last_fill_activity_ns_ >
                            UINT64_MAX - kPostFillQuietPeriodNs
                        ? UINT64_MAX
                        : last_fill_activity_ns_ + kPostFillQuietPeriodNs;
                if (maintenance_due && quiet_deadline > now) {
                    deadline = quiet_deadline;
                } else if (quiet_deadline > now && quiet_deadline < deadline) {
                    deadline = quiet_deadline;
                }
            }
            if (deadline > now) {
                constexpr uint64_t kNanosecondsPerMillisecond = 1'000'000ULL;
                const uint64_t milliseconds =
                    (deadline - now + kNanosecondsPerMillisecond - 1U) /
                    kNanosecondsPerMillisecond;
                timeout_ms = milliseconds > static_cast<uint64_t>(INT_MAX)
                    ? INT_MAX : static_cast<int>(milliseconds);
            }
        }

        const int ready = ::poll(poll_fds_.data(), poll_fds_.size(), timeout_ms);
        if (ready > 0) {
            // A queued fill takes precedence over client I/O. Continue to
            // the top so fanout is measured from callback to outbound-buffer
            // queueing, not after an O(active_connections) readiness pass.
            if (!queue_work_remains && wake_read_fd_ >= 0 &&
                (poll_fds_[0].revents &
                 (POLLIN | POLLPRI | POLLERR | POLLHUP)) != 0) {
                continue;
            }
            const uint64_t io_now = monotonic_now_ns();
            if (connections_) {
                for (uint32_t index = 0; index < max_connections_; ++index) {
                    Connection& connection = connections_[index];
                    if (connection.fd < 0) continue;
                    const short events = poll_fds_[index + 1U].revents;
                    poll_fds_[index + 1U].revents = 0;
                    if ((events & (POLLIN | POLLPRI)) != 0) {
                        (void)receive(connection, io_now);
                    }
                    if (connection.fd >= 0 &&
                        ((events & POLLOUT) != 0 ||
                         (connection.close_after_flush &&
                          connection.write_offset != connection.write_size))) {
                        (void)flush(connection, io_now);
                    }
                    if (connection.fd >= 0 &&
                        (events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                        // A peer may half-close immediately after a close frame.
                        // Preserve the queued close echo through at least the
                        // nonblocking flush above; a failed/bounded flush is
                        // still cleaned up by the normal error/backpressure path.
                        if (!connection.close_after_flush ||
                            connection.write_offset == connection.write_size) {
                            close(connection);
                        }
                    }
                }
            }
        }
    }
}

}  // namespace luv::ws
