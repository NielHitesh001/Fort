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
  auto parse = [](std::initializer_list<uint8_t> input, bool &fragmented) {
    std::array<char, 2048> b{};
    size_t n = 0;
    for (auto c : input)
      b[n++] = static_cast<char>(c);
    Frame out{};
    return frame(b.data(), n, fragmented, out);
  };
  bool fragmented = false;
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
        fragmented);
  CHECK(parse({0x89, 0x80, 0, 0, 0, 0}, fragmented) == FrameResult::kConsumed &&
        fragmented);
  CHECK(parse({0x80, 0x80, 0, 0, 0, 0}, fragmented) == FrameResult::kConsumed &&
        !fragmented);
  CHECK(parse({0x88, 0x81, 0, 0, 0, 0, 0}, fragmented) ==
        FrameResult::kProtocolError);
  CHECK(parse({0x88, 0x82, 0, 0, 0, 0, 3, 0xed}, fragmented) ==
        FrameResult::kProtocolError);
  CHECK(parse({0x88, 0x83, 0, 0, 0, 0, 3, 0xe8, 0xff}, fragmented) ==
        FrameResult::kInvalidUtf8);
  CHECK(parse({0x88, 0x82, 1, 2, 3, 4, 2, 0xea}, fragmented) ==
        FrameResult::kConsumed);
}
