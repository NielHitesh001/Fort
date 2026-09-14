#pragma once
#include "luv_wire_parse.hpp"
#include <array>
#include <cstring>
namespace luv::wire {
constexpr size_t kDecodedKeyBytes = 16, kWebSocketKeyBytes = 24;
// A client frame must fit in the fixed 2 KiB connection read buffer, including
// its largest possible 14-byte header.  Apply the same bound to the complete
// logical message so a continuation sequence cannot turn that fixed buffer
// into unbounded aggregate state.
constexpr size_t kMaxWebSocketMessageBytes = 2048 - 14;
[[nodiscard]] inline int base64_value(const char value) noexcept {
  if (value >= 'A' && value <= 'Z')
    return value - 'A';
  if (value >= 'a' && value <= 'z')
    return value - 'a' + 26;
  if (value >= '0' && value <= '9')
    return value - '0' + 52;
  if (value == '+')
    return 62;
  if (value == '/')
    return 63;
  return -1;
}

// RFC 6455 requires a base64 representation of exactly 16 random bytes.  The
// last quartet must therefore be XX== and its unused low bits must be zero.
[[nodiscard]] inline bool
decode_websocket_key(const char *key,
                     std::array<uint8_t, kDecodedKeyBytes> &decoded) noexcept {
  if (!key)
    return false;

  size_t length = 0;
  while (length <= kWebSocketKeyBytes && key[length] != '\0')
    ++length;
  if (length != kWebSocketKeyBytes || key[22] != '=' || key[23] != '=') {
    return false;
  }

  size_t output = 0;
  for (size_t input = 0; input < 20; input += 4) {
    const int a = base64_value(key[input]);
    const int b = base64_value(key[input + 1]);
    const int c = base64_value(key[input + 2]);
    const int d = base64_value(key[input + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0)
      return false;

    decoded[output++] = static_cast<uint8_t>((a << 2) | (b >> 4));
    decoded[output++] = static_cast<uint8_t>((b << 4) | (c >> 2));
    decoded[output++] = static_cast<uint8_t>((c << 6) | d);
  }

  const int a = base64_value(key[20]);
  const int b = base64_value(key[21]);
  if (a < 0 || b < 0 || (b & 0x0F) != 0)
    return false;
  decoded[output++] = static_cast<uint8_t>((a << 2) | (b >> 4));
  return output == decoded.size();
}

[[nodiscard]] inline bool valid_close_code(const uint16_t code) noexcept {
  if (code >= 3000 && code <= 4999)
    return true;
  return code >= 1000 && code <= 1014 && code != 1004 && code != 1005 &&
         code != 1006;
}

// RFC 6455 §5.5.1 requires a close reason to be valid UTF-8.  Decode only
// enough to reject malformed, overlong, surrogate, and out-of-range forms;
// the payload remains in its fixed connection buffer.
[[nodiscard]] inline bool valid_utf8(const char *text,
                                     const size_t size) noexcept {
  if (size != 0 && !text)
    return false;
  size_t offset = 0;
  while (offset < size) {
    const uint8_t first = static_cast<uint8_t>(text[offset++]);
    if (first <= 0x7FU)
      continue;

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
    if (offset + continuation_count > size)
      return false;
    for (uint8_t index = 0; index < continuation_count; ++index) {
      const uint8_t next = static_cast<uint8_t>(text[offset++]);
      if ((next & 0xC0U) != 0x80U)
        return false;
      code_point = (code_point << 6U) | (next & 0x3FU);
    }
    if (code_point < minimum || code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
      return false;
    }
  }
  return true;
}

struct HeaderRange {
  const char *begin = nullptr;
  const char *end = nullptr;
};
inline char ascii_lower(char value) noexcept {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a')
                                      : value;
}
inline bool ascii_equal(const char *left, size_t left_size,
                        const char *right) noexcept {
  const size_t right_size = std::strlen(right);
  if (left_size != right_size)
    return false;
  for (size_t i = 0; i < left_size; ++i)
    if (ascii_lower(left[i]) != ascii_lower(right[i]))
      return false;
  return true;
}
inline HeaderRange header(const char *request, const char *block_end,
                          const char *wanted_name) noexcept {
  const char *line = std::strstr(request, "\r\n");
  while (line && line < block_end) {
    line += 2;
    if (line >= block_end)
      break;
    const char *line_end = std::strstr(line, "\r\n");
    if (!line_end || line_end > block_end)
      break;
    const char *colon = line;
    while (colon < line_end && *colon != ':')
      ++colon;
    if (colon < line_end &&
        ascii_equal(line, size_t(colon - line), wanted_name)) {
      const char *value_begin = colon + 1;
      while (value_begin < line_end &&
             (*value_begin == ' ' || *value_begin == '\t'))
        ++value_begin;
      const char *value_end = line_end;
      while (value_end > value_begin &&
             (value_end[-1] == ' ' || value_end[-1] == '\t'))
        --value_end;
      return {value_begin, value_end};
    }
    line = line_end;
  }
  return {};
}
inline bool header_has_token(HeaderRange value, const char *token) noexcept {
  while (value.begin && value.begin < value.end) {
    while (value.begin < value.end &&
           (*value.begin == ' ' || *value.begin == '\t' || *value.begin == ','))
      ++value.begin;
    const char *token_end = value.begin;
    while (token_end < value.end && *token_end != ',')
      ++token_end;
    const char *trimmed_end = token_end;
    while (trimmed_end > value.begin &&
           (trimmed_end[-1] == ' ' || trimmed_end[-1] == '\t'))
      --trimmed_end;
    if (ascii_equal(value.begin, size_t(trimmed_end - value.begin), token))
      return true;
    value.begin = token_end < value.end ? token_end + 1 : value.end;
  }
  return false;
}

// Requires a completed, validated HTTP header block. The wrapper below supplies
// it.
inline bool handshake(std::string_view bytes) noexcept {
  auto r = request(bytes);
  if (r.status != Status::complete || r.method != "GET" ||
      r.required_size != r.header_size)
    return false;
  std::array<char, 2048> copy{};
  std::memcpy(copy.data(), bytes.data(), r.header_size);
  const char *end = copy.data() + r.header_size - 4;
  auto k = header(copy.data(), end, "Sec-WebSocket-Key");
  auto v = header(copy.data(), end, "Sec-WebSocket-Version");
  if (!k.begin || k.end - k.begin != 24 || !v.begin ||
      !ascii_equal(v.begin, size_t(v.end - v.begin), "13") ||
      !header_has_token(header(copy.data(), end, "Connection"), "Upgrade") ||
      !header_has_token(header(copy.data(), end, "Upgrade"), "websocket"))
    return false;
  std::array<char, 25> key{};
  std::memcpy(key.data(), k.begin, 24);
  std::array<uint8_t, 16> decoded{};
  return decode_websocket_key(key.data(), decoded);
}
enum class FrameResult : uint8_t {
  kIncomplete,
  kConsumed,
  kProtocolError,
  kMessageTooLarge,
  kInvalidUtf8
};
struct Frame {
  size_t size = 0, payload_offset = 0, payload_size = 0;
  uint8_t opcode = 0;
};
struct FragmentState {
  bool fragmented = false;
  uint8_t opcode = 0;
  size_t message_size = 0;
  // Text is retained only until its final continuation so UTF-8 is checked on
  // the completed logical message. Binary messages need only their size.
  std::array<char, kMaxWebSocketMessageBytes> text{};
};
// On success unmasks exactly one frame in place; incomplete input leaves state
// unchanged.
inline FrameResult frame(char *buffer, size_t size, FragmentState &state,
                         Frame &output) noexcept {
  // RFC 6455 inbound layout:
  //   byte 0: FIN | RSV1..3 | opcode
  //   byte 1: MASK | payload length selector
  //   optional 16/64-bit length, then mandatory client mask key, payload.
  // Client payload bytes are unmasked in-place before control/data handling.
  if (size < 2)
    return FrameResult::kIncomplete;

  const auto *input = reinterpret_cast<const uint8_t *>(buffer);
  const uint8_t first = input[0];
  const uint8_t second = input[1];
  const bool final = (first & 0x80U) != 0;
  const uint8_t opcode = first & 0x0FU;
  if (opcode != 0 && opcode != 1 && opcode != 2 && opcode != 8 && opcode != 9 &&
      opcode != 10)
    return FrameResult::kProtocolError;
  const bool masked = (second & 0x80U) != 0;
  const uint8_t length_marker = second & 0x7FU;
  if ((first & 0x70U) != 0 || !masked)
    return FrameResult::kProtocolError;

  size_t header_size = 2;
  uint64_t payload_size_64 = 0;
  if (length_marker <= 125) {
    payload_size_64 = length_marker;
  } else if (length_marker == 126) {
    if (size < 4)
      return FrameResult::kIncomplete;
    payload_size_64 = (static_cast<uint64_t>(input[2]) << 8U) | input[3];
    if (payload_size_64 < 126)
      return FrameResult::kProtocolError;
    header_size = 4;
  } else {
    if (size < 10)
      return FrameResult::kIncomplete;
    if ((input[2] & 0x80U) != 0)
      return FrameResult::kProtocolError;
    for (size_t index = 0; index < 8; ++index) {
      payload_size_64 = (payload_size_64 << 8U) | input[2U + index];
    }
    if (payload_size_64 <= 0xFFFFU)
      return FrameResult::kProtocolError;
    header_size = 10;
  }

  const bool control = (opcode & 0x08U) != 0;
  if (control && (!final || payload_size_64 > 125)) {
    return FrameResult::kProtocolError;
  }
  if (payload_size_64 > kMaxWebSocketMessageBytes) {
    return FrameResult::kMessageTooLarge;
  }

  const size_t payload_size = static_cast<size_t>(payload_size_64);
  const size_t frame_size = header_size + 4U + payload_size;
  if (size < frame_size)
    return FrameResult::kIncomplete;

  const uint8_t *mask = input + header_size;
  char *payload = buffer + header_size + 4U;
  for (size_t index = 0; index < payload_size; ++index) {
    payload[index] = static_cast<char>(static_cast<uint8_t>(payload[index]) ^
                                       mask[index & 0x03U]);
  }

  if (opcode == 8) {
    if (payload_size == 1)
      return FrameResult::kProtocolError;
    if (payload_size >= 2) {
      const uint16_t code = static_cast<uint16_t>(
          (static_cast<uint16_t>(static_cast<uint8_t>(payload[0])) << 8U) |
          static_cast<uint8_t>(payload[1]));
      if (!valid_close_code(code))
        return FrameResult::kProtocolError;
    }
    if (payload_size > 2 && !valid_utf8(payload + 2, payload_size - 2)) {
      return FrameResult::kInvalidUtf8;
    }
  }
  if (!control) {
    if (opcode == 0x00U) {
      if (!state.fragmented)
        return FrameResult::kProtocolError;
      if (payload_size > kMaxWebSocketMessageBytes - state.message_size)
        return FrameResult::kMessageTooLarge;
      if (state.opcode == 0x01U) {
        std::memcpy(state.text.data() + state.message_size, payload,
                    payload_size);
        if (final && !valid_utf8(state.text.data(),
                                 state.message_size + payload_size)) {
          return FrameResult::kInvalidUtf8;
        }
      }
      if (final) {
        state = FragmentState{};
      } else {
        state.message_size += payload_size;
      }
    } else if (opcode == 0x01U || opcode == 0x02U) {
      if (state.fragmented)
        return FrameResult::kProtocolError;
      if (opcode == 0x01U && final && !valid_utf8(payload, payload_size))
        return FrameResult::kInvalidUtf8;
      if (!final) {
        state.fragmented = true;
        state.opcode = opcode;
        state.message_size = payload_size;
        if (opcode == 0x01U)
          std::memcpy(state.text.data(), payload, payload_size);
      }
    } else {
      return FrameResult::kProtocolError;
    }
  }
  output = {frame_size, header_size + 4, payload_size, opcode};
  return FrameResult::kConsumed;
}
} // namespace luv::wire
