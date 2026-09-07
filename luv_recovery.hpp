#pragma once

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace luv {

enum class RecoveryEventType : uint8_t {
    kAdd = 1,
    kFill = 2,
    kCancel = 3,
    kAck = 4,
    kNew = 5,
    kReplace = 6,
};

constexpr uint8_t kRecoveryRecordVersion = 2;

struct RecoveryEvent {
    RecoveryEventType type = RecoveryEventType::kNew;
    uint64_t order_id = 0;
    int64_t quantity = 0;
    int64_t price = 0;
    uint8_t side = 0;
    uint64_t timestamp_ns = 0;
};

struct RecoveryRecord {
    uint32_t magic = 0x31564352; // "RCV1"
    uint8_t version = kRecoveryRecordVersion;
    uint8_t type = 0;
    uint16_t reserved = 0;
    uint64_t sequence = 0;
    uint64_t order_id = 0;
    int64_t quantity = 0;
    int64_t price = 0;
    uint8_t side = 0;
    uint8_t reserved_bytes[7]{};
    uint64_t timestamp_ns = 0;
    uint32_t checksum = 0;
    uint32_t padding = 0;
};
static_assert(sizeof(RecoveryRecord) == 64, "Recovery record layout changed");

struct RecoveredOrder {
    uint64_t order_id = 0;
    int64_t remaining = 0;
    int64_t price = 0;
    uint8_t side = 0;
    uint64_t timestamp_ns = 0;
};

class RecoveryLedger {
public:
    RecoveryLedger() = default;
    ~RecoveryLedger() { close(); }
    RecoveryLedger(const RecoveryLedger&) = delete;
    RecoveryLedger& operator=(const RecoveryLedger&) = delete;

    [[nodiscard]] bool open(const char* path) noexcept {
        if (!path || _fd >= 0) return false;
        _fd = ::open(path, O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
        if (_fd < 0) return false;

        struct stat metadata{};
        if (::fstat(_fd, &metadata) != 0 ||
            metadata.st_size % static_cast<off_t>(sizeof(RecoveryRecord)) != 0) {
            close();
            return false;
        }
        _sequence = static_cast<uint64_t>(
            metadata.st_size / static_cast<off_t>(sizeof(RecoveryRecord)));
        return true;
    }

    [[nodiscard]] bool append(RecoveryEventType type, uint64_t order_id,
                              int64_t quantity, int64_t price = 0) noexcept {
        return append_with_side(type, order_id, quantity, price, 0, 0);
    }

    [[nodiscard]] bool append_with_side(RecoveryEventType type, uint64_t order_id,
                                       int64_t quantity, int64_t price,
                                       uint8_t side, uint64_t timestamp_ns) noexcept {
        if (_fd < 0 || order_id == 0 || quantity <= 0) return false;
        RecoveryRecord record{};
        record.type = static_cast<uint8_t>(type);
        record.sequence = _sequence;
        record.order_id = order_id;
        record.quantity = quantity;
        record.price = price;
        record.side = side;
        record.timestamp_ns = timestamp_ns;
        record.checksum = checksum(record);
        if (!write_all(&record, sizeof(record))) return false;
        if (::fsync(_fd) != 0) return false;
        ++_sequence;

        return true;
    }

    [[nodiscard]] bool append_and_flush(const RecoveryEvent& event) noexcept {
        return append_with_side(
            event.type,
            event.order_id,
            event.quantity,
            event.price,
            event.side,
            event.timestamp_ns);
    }

    [[nodiscard]] bool replay(RecoveredOrder* output, uint32_t capacity,
                              uint32_t& count) noexcept {
        count = 0;
        if (_fd < 0 || !output || capacity == 0) return false;
        struct stat metadata{};
        if (::fstat(_fd, &metadata) != 0 ||
            metadata.st_size % static_cast<off_t>(sizeof(RecoveryRecord)) != 0)
            return false;
        if (metadata.st_size == 0) return true;

        const size_t bytes = static_cast<size_t>(metadata.st_size);
        const void* mapped = ::mmap(nullptr, bytes, PROT_READ, MAP_PRIVATE,
                                    _fd, 0);
        if (mapped == MAP_FAILED) return false;

        const auto* records = static_cast<const RecoveryRecord*>(mapped);
        const uint64_t record_count = bytes / sizeof(RecoveryRecord);
        uint64_t expected_sequence = 0;
        for (uint64_t index = 0; index < record_count; ++index) {
            const RecoveryRecord& record = records[index];
            if (record.magic != 0x31564352 ||
                record.version != kRecoveryRecordVersion ||
                record.sequence != expected_sequence ||
                checksum(record) != record.checksum) {
                ::munmap(const_cast<RecoveryRecord*>(records), bytes);
                return false;
            }
            if (!apply(record, output, capacity, count)) {
                ::munmap(const_cast<RecoveryRecord*>(records), bytes);
                return false;
            }
            ++expected_sequence;
        }
        ::munmap(const_cast<RecoveryRecord*>(records), bytes);
        return true;
    }

    void close() noexcept {
        if (_fd >= 0) {
            (void)::fsync(_fd);
            (void)::close(_fd);
            _fd = -1;
        }
    }

    [[nodiscard]] uint64_t size() const noexcept { return _sequence; }

private:
    [[nodiscard]] static uint32_t checksum(const RecoveryRecord& record) noexcept {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
        uint32_t hash = 2166136261u;
        for (size_t index = 0; index < offsetof(RecoveryRecord, checksum); ++index) {
            hash ^= bytes[index];
            hash *= 16777619u;
        }
        return hash;
    }

    [[nodiscard]] bool apply(const RecoveryRecord& record,
                             RecoveredOrder* output, uint32_t capacity,
                             uint32_t& count) noexcept {
        uint32_t index = 0;
        while (index < count && output[index].order_id != record.order_id) ++index;
        if (record.type == static_cast<uint8_t>(RecoveryEventType::kAdd) ||
            record.type == static_cast<uint8_t>(RecoveryEventType::kNew)) {
            if (index != count || count == capacity) return false;
            output[count++] = {record.order_id, record.quantity, record.price,
                               record.side, record.timestamp_ns};
            return true;
        }
        if (index == count) return false;
        if (record.type == static_cast<uint8_t>(RecoveryEventType::kFill)) {
            if (record.quantity > output[index].remaining) return false;
            output[index].remaining -= record.quantity;
            if (output[index].remaining == 0) remove(output, count, index);
            return true;
        }
        if (record.type == static_cast<uint8_t>(RecoveryEventType::kReplace)) {
            if (record.quantity <= 0) return false;
            output[index].remaining = record.quantity;
            if (record.price > 0) output[index].price = record.price;
            return true;
        }
        if (record.type == static_cast<uint8_t>(RecoveryEventType::kCancel)) {
            remove(output, count, index);
            return true;
        }
        return false;
    }

    static void remove(RecoveredOrder* output, uint32_t& count,
                       uint32_t index) noexcept {
        output[index] = output[count - 1];
        --count;
    }

    [[nodiscard]] bool write_all(const void* data, size_t bytes) noexcept {
        const auto* cursor = static_cast<const uint8_t*>(data);
        while (bytes != 0) {
            const ssize_t written = ::write(_fd, cursor, bytes);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) return false;
            cursor += written;
            bytes -= static_cast<size_t>(written);
        }
        return true;
    }

    int _fd = -1;
    uint64_t _sequence = 0;
};

}  // namespace luv
