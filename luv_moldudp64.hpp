#pragma once

#include <cstddef>
#include <cstdint>

namespace luv::moldudp64 {

inline constexpr std::size_t kHeaderSize = 20;

struct Header {
    uint64_t sequence = 0;
    uint16_t message_count = 0;
};

[[nodiscard]] inline uint16_t read_be16(const uint8_t* data) noexcept {
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

[[nodiscard]] inline uint64_t read_be64(const uint8_t* data) noexcept {
    uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index)
        value = (value << 8) | data[index];
    return value;
}

[[nodiscard]] inline bool parse_header(const uint8_t* data, std::size_t size,
                                       Header& header) noexcept {
    if (!data || size < kHeaderSize) return false;
    header.sequence = read_be64(data + 10);
    header.message_count = read_be16(data + 18);
    return true;
}

[[nodiscard]] inline bool validate_message_block(const uint8_t* data,
                                                 std::size_t size,
                                                 uint16_t message_count) noexcept {
    if (!data || size < kHeaderSize) return false;

    std::size_t offset = kHeaderSize;
    for (uint16_t index = 0; index < message_count; ++index) {
        if (size - offset < sizeof(uint16_t)) return false;
        const uint16_t length = read_be16(data + offset);
        offset += sizeof(uint16_t);
        if (length == 0 || length > size - offset) return false;
        offset += length;
    }
    return offset == size;
}

class MessageIterator {
public:
    MessageIterator(const uint8_t* data, std::size_t size) noexcept
        : _data(data), _size(size) {}

    [[nodiscard]] bool next(const uint8_t*& message,
                            uint16_t& length) noexcept {
        if (_invalid || !_data || _offset == _size) return false;
        if (_offset > _size || _size - _offset < sizeof(uint16_t)) {
            _invalid = true;
            return false;
        }

        length = read_be16(_data + _offset);
        _offset += sizeof(uint16_t);
        if (length == 0 || length > _size - _offset) {
            _invalid = true;
            return false;
        }

        message = _data + _offset;
        _offset += length;
        return true;
    }

    [[nodiscard]] bool valid() const noexcept { return !_invalid; }

private:
    const uint8_t* _data = nullptr;
    std::size_t _size = 0;
    std::size_t _offset = kHeaderSize;
    bool _invalid = false;
};

struct RequestPacket {
    char session[10]{};
    uint64_t sequence = 0;
    uint16_t requested_count = 0;
};

[[nodiscard]] inline bool format_request(const RequestPacket& req, uint8_t* buffer, std::size_t size) noexcept {
    if (!buffer || size < 20) return false;
    std::memcpy(buffer, req.session, 10);
    // Write 64-bit sequence (BE)
    for (int i = 7; i >= 0; --i) {
        buffer[10 + i] = static_cast<uint8_t>((req.sequence >> ((7 - i) * 8)) & 0xFF);
    }
    // Write 16-bit count (BE)
    buffer[18] = static_cast<uint8_t>((req.requested_count >> 8) & 0xFF);
    buffer[19] = static_cast<uint8_t>(req.requested_count & 0xFF);
    return true;
}

[[nodiscard]] inline bool parse_request(const uint8_t* data, std::size_t size, RequestPacket& req) noexcept {
    if (!data || size < 20) return false;
    std::memcpy(req.session, data, 10);
    req.sequence = read_be64(data + 10);
    req.requested_count = read_be16(data + 18);
    return true;
}

}  // namespace luv::moldudp64