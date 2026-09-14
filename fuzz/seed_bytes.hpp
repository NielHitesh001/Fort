#pragma once
#include <array>
#include <cstring>
#include <string_view>
// Textual hex keeps binary seed bytes reviewable in git. Non-prefixed fuzz
// inputs go through unchanged; the parser never sees this corpus convention.
inline size_t seed_bytes(std::string_view bytes,
                         std::array<char, 4096> &buffer) {
  size_t count = 0;
  if (bytes.starts_with("hex:")) {
    int high = -1;
    for (char c : bytes.substr(4)) {
      int digit = c >= '0' && c <= '9'   ? c - '0'
                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                         : -1;
      if (digit < 0)
        continue;
      if (high < 0)
        high = digit;
      else {
        if (count == buffer.size())
          break;
        buffer[count++] = static_cast<char>((high << 4) | digit);
        high = -1;
      }
    }
  } else {
    count = bytes.size() < buffer.size() ? bytes.size() : buffer.size();
    if (count)
      std::memcpy(buffer.data(), bytes.data(), count);
  }
  return count;
}
