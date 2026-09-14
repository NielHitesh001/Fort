#include "luv_websocket_parse.hpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::fprintf(stderr, "line %d: %s\n", __LINE__, #x);                     \
      std::abort();                                                            \
    }                                                                          \
  } while (0)
int main() {
  using namespace luv::wire;
  CHECK(request("").status == Status::incomplete);
  CHECK(request("GET / HTTP/1.1\r\n").status == Status::incomplete);
  CHECK(request("GET / HTTP/9.9\r\n\r\n").status == Status::invalid);
  CHECK(request(
            "GET / HTTP/1.1\r\nContent-Length: 0\r\ncontent-length: 0\r\n\r\n")
            .status == Status::invalid);
  CHECK(
      request("GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n").status ==
      Status::invalid);
  CHECK(request("GET / HTTP/1.1\r\nBad Header: x\r\n\r\n").status ==
        Status::invalid);
  CHECK(
      request("POST / HTTP/1.1\r\nContent-Length: 18446744073709551616\r\n\r\n")
          .status == Status::too_large);
  const std::string_view body =
      "POST / HTTP/1.1\r\nContent-Length: 2\r\n\r\n{}extra";
  auto r = request(body);
  CHECK(r.status == Status::complete && r.required_size == body.size() - 5);
  CHECK(request(body.substr(0, r.required_size - 1)).status ==
        Status::incomplete);
  const std::string_view upgrade =
      "GET /api/v1/stream/1 HTTP/1.1\r\nUpgrade: websocket\r\nConnection: "
      "Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: "
      "dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
  CHECK(handshake(upgrade));
  for (size_t n = 0; n < upgrade.size(); ++n)
    CHECK(!handshake(upgrade.substr(0, n)));
  auto parse = [](std::initializer_list<uint8_t> input, FragmentState &fragmented) {
    std::array<char, 2048> b{};
    size_t n = 0;
    for (auto c : input)
      b[n++] = static_cast<char>(c);
    Frame out{};
    return frame(b.data(), n, fragmented, out);
  };
  FragmentState fragmented{};
  CHECK(parse({0x8b, 0x80}, fragmented) == FrameResult::kProtocolError);
  CHECK(parse({0x81, 0}, fragmented) == FrameResult::kProtocolError);
  CHECK(parse({0x81, 0xfe, 0}, fragmented) == FrameResult::kIncomplete);
  CHECK(parse({0x82, 0xff, 0x80, 0, 0, 0, 0, 0, 0, 0}, fragmented) ==
        FrameResult::kProtocolError);
  CHECK(parse({0x82, 0xfe, 0x10, 0}, fragmented) ==
        FrameResult::kMessageTooLarge);
  CHECK(parse({0x80, 0x80, 0, 0, 0, 0}, fragmented) ==
        FrameResult::kProtocolError);
  CHECK(parse({0x02, 0x80, 0, 0, 0, 0}, fragmented) == FrameResult::kConsumed &&
        fragmented.fragmented);
  CHECK(parse({0x89, 0x80, 0, 0, 0, 0}, fragmented) == FrameResult::kConsumed &&
        fragmented.fragmented);
  CHECK(parse({0x80, 0x80, 0, 0, 0, 0}, fragmented) == FrameResult::kConsumed &&
        !fragmented.fragmented);
  CHECK(parse({0x88, 0x81, 0, 0, 0, 0, 0}, fragmented) ==
        FrameResult::kProtocolError);
  CHECK(parse({0x88, 0x82, 0, 0, 0, 0, 3, 0xed}, fragmented) ==
        FrameResult::kProtocolError);
  CHECK(parse({0x88, 0x83, 0, 0, 0, 0, 3, 0xe8, 0xff}, fragmented) ==
        FrameResult::kInvalidUtf8);
  CHECK(parse({0x88, 0x82, 1, 2, 3, 4, 2, 0xea}, fragmented) ==
        FrameResult::kConsumed);

  auto parse_payload = [](uint8_t first, const char *payload,
                          size_t payload_size, FragmentState &state) {
    std::array<char, 2048> bytes{};
    CHECK(payload_size <= kMaxWebSocketMessageBytes);
    bytes[0] = static_cast<char>(first);
    const size_t payload_offset = payload_size <= 125U ? 6U : 8U;
    bytes[1] = static_cast<char>(0x80U | (payload_size <= 125U ? payload_size : 126U));
    if (payload_size > 125U) {
      bytes[2] = static_cast<char>(payload_size >> 8U);
      bytes[3] = static_cast<char>(payload_size);
    }
    std::memcpy(bytes.data() + payload_offset, payload, payload_size); // zero mask key
    Frame output{};
    return frame(bytes.data(), payload_offset + payload_size, state, output);
  };
  std::array<char, 1017> first_fragment{};
  std::array<char, 1018> second_fragment{};
  first_fragment.fill('a');
  second_fragment.fill('b');
  FragmentState oversized{};
  CHECK(parse_payload(0x01U, first_fragment.data(), first_fragment.size(),
                      oversized) == FrameResult::kConsumed);
  CHECK(oversized.fragmented && oversized.message_size == first_fragment.size());
  CHECK(parse_payload(0x80U, second_fragment.data(), second_fragment.size(),
                      oversized) == FrameResult::kMessageTooLarge);

  FragmentState split_utf8{};
  const char leading_byte[] = {static_cast<char>(0xc3U)};
  const char trailing_byte[] = {static_cast<char>(0xa9U)};
  CHECK(parse_payload(0x01U, leading_byte, sizeof(leading_byte), split_utf8) ==
        FrameResult::kConsumed);
  CHECK(parse_payload(0x80U, trailing_byte, sizeof(trailing_byte), split_utf8) ==
        FrameResult::kConsumed);
  CHECK(!split_utf8.fragmented);
  FragmentState invalid_split_utf8{};
  const char invalid_trailing_byte[] = {'x'};
  CHECK(parse_payload(0x01U, leading_byte, sizeof(leading_byte),
                      invalid_split_utf8) == FrameResult::kConsumed);
  CHECK(parse_payload(0x80U, invalid_trailing_byte,
                      sizeof(invalid_trailing_byte), invalid_split_utf8) ==
        FrameResult::kInvalidUtf8);
}
