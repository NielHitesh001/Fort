#include "luv_websocket_parse.hpp"
#include "seed_bytes.hpp"
#include <cstdlib>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::array<char, 4096> buffer{};
  const size_t count =
      seed_bytes({reinterpret_cast<const char *>(data), size}, buffer);
  const std::string_view bytes(buffer.data(), count);
  (void)luv::wire::handshake(bytes);
  (void)luv::wire::handshake(bytes.substr(0, count / 2));
  luv::wire::FragmentState fragmented{};
  size_t offset = 0;
  while (offset < count) {
    luv::wire::Frame frame{};
    auto result = luv::wire::frame(buffer.data() + offset, count - offset,
                                   fragmented, frame);
    if (result != luv::wire::FrameResult::kConsumed)
      break;
    if (!frame.size || frame.size > count - offset ||
        frame.payload_offset + frame.payload_size != frame.size)
      std::abort();
    offset += frame.size;
    if (frame.opcode == 8)
      break;
  }
  return 0;
}
