#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

#include "luv_moldudp64.hpp"

static void write_be16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value >> 8);
    data[1] = static_cast<uint8_t>(value);
}

static void write_be64(uint8_t* data, uint64_t value) {
    for (int index = 7; index >= 0; --index) {
        data[index] = static_cast<uint8_t>(value);
        value >>= 8;
    }
}

int main() {
    std::array<uint8_t, 28> packet{};
    write_be64(packet.data() + 10, 0x0102030405060708ULL);
    write_be16(packet.data() + 18, 2);
    write_be16(packet.data() + 20, 3);
    packet[22] = 'A';
    packet[23] = 'B';
    packet[24] = 'C';
    write_be16(packet.data() + 25, 1);
    packet[27] = 'X';

    luv::moldudp64::Header header{};
    assert(luv::moldudp64::parse_header(packet.data(), packet.size(), header));
    assert(header.sequence == 0x0102030405060708ULL);
    assert(header.message_count == 2);
    assert(luv::moldudp64::validate_message_block(
        packet.data(), packet.size(), header.message_count));

    luv::moldudp64::MessageIterator messages(packet.data(), packet.size());
    const uint8_t* message = nullptr;
    uint16_t length = 0;
    assert(messages.next(message, length));
    assert(length == 3 && message[0] == 'A' && message[2] == 'C');
    assert(messages.next(message, length));
    assert(length == 1 && message[0] == 'X');
    assert(!messages.next(message, length));
    assert(messages.valid());

    std::array<uint8_t, 23> truncated{};
    for (std::size_t index = 0; index < truncated.size(); ++index)
        truncated[index] = packet[index];
    truncated[20] = 0;
    truncated[21] = 4;
    luv::moldudp64::MessageIterator invalid(truncated.data(), truncated.size());
    assert(!invalid.next(message, length));
    assert(!invalid.valid());
    assert(!luv::moldudp64::validate_message_block(
        truncated.data(), truncated.size(), header.message_count));

    std::array<uint8_t, 28> wrong_count = packet;
    write_be16(wrong_count.data() + 18, 1);
    assert(!luv::moldudp64::validate_message_block(
        wrong_count.data(), wrong_count.size(), 1));

    std::puts("MoldUDP64 header and message fixture checks passed.");
    return 0;
}