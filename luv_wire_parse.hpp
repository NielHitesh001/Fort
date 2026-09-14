#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

// Allocation-free wire framing shared by the listeners and byte-buffer fuzzers.
namespace luv::wire {
enum class Status { incomplete, complete, invalid, too_large };
struct Request {
  Status status = Status::incomplete;
  size_t header_size = 0, required_size = 0;
  std::string_view method{}, path{};
};
inline bool equal(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; };
    if (lower(a[i]) != lower(b[i]))
      return false;
  }
  return true;
}
inline bool token(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') ||
         std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
}
inline std::string_view trim(std::string_view v) noexcept {
  while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
    v.remove_prefix(1);
  while (!v.empty() && (v.back() == ' ' || v.back() == '\t'))
    v.remove_suffix(1);
  return v;
}
inline Request request(std::string_view bytes, size_t limit = 2047) noexcept {
  Request r;
  auto fail = [&](Status s) {
    r.status = s;
    return r;
  };
  const auto end = bytes.find("\r\n\r\n");
  if (end == bytes.npos)
    return fail(bytes.size() >= limit ? Status::too_large : Status::incomplete);
  r.header_size = end + 4;
  if (r.header_size > limit)
    return fail(Status::too_large);
  const auto first = bytes.find("\r\n");
  auto line = bytes.substr(0, first);
  auto space = line.find(' ');
  if (space == line.npos)
    return fail(Status::invalid);
  r.method = line.substr(0, space);
  line.remove_prefix(space + 1);
  space = line.find(' ');
  if (space == line.npos)
    return fail(Status::invalid);
  r.path = line.substr(0, space);
  if (r.method.empty() || r.method.size() > 7 || r.path.empty() ||
      r.path.size() > 127 || r.path.front() != '/' ||
      line.substr(space + 1) != "HTTP/1.1")
    return fail(Status::invalid);
  for (char c : r.method)
    if (!token(c))
      return fail(Status::invalid);
  for (unsigned char c : r.path)
    if (c <= 32 || c >= 127)
      return fail(Status::invalid);
  size_t length = 0;
  bool seen_length = false;
  // Duplicate security/framing headers are ambiguous: reject, never first-wins.
  unsigned seen = 0;
  for (size_t pos = first + 2; pos < end;) {
    const auto next = bytes.find("\r\n", pos);
    auto field = bytes.substr(pos, next - pos);
    const auto colon = field.find(':');
    if (colon == field.npos || colon == 0)
      return fail(Status::invalid);
    auto name = field.substr(0, colon);
    for (char c : name)
      if (!token(c))
        return fail(Status::invalid);
    auto value = trim(field.substr(colon + 1));
    for (unsigned char c : value)
      if ((c < 32 && c != 9) || c == 127)
        return fail(Status::invalid);
    constexpr std::string_view unique[] = {
        "authorization",     "host",
        "upgrade",           "connection",
        "sec-websocket-key", "sec-websocket-version"};
    for (unsigned i = 0; i < 6; ++i)
      if (equal(name, unique[i])) {
        if (seen & (1U << i))
          return fail(Status::invalid);
        seen |= 1U << i;
      }
    if (equal(name, "transfer-encoding"))
      return fail(Status::invalid);
    if (equal(name, "content-length")) {
      if (seen_length || value.empty())
        return fail(Status::invalid);
      seen_length = true;
      for (char c : value) {
        if (c < '0' || c > '9')
          return fail(Status::invalid);
        const size_t digit = static_cast<size_t>(c - '0');
        if (length > (SIZE_MAX - digit) / 10)
          return fail(Status::too_large);
        length = length * 10 + digit;
      }
    }
    pos = next + 2;
  }
  if (length > limit - r.header_size)
    return fail(Status::too_large);
  r.required_size = r.header_size + length;
  r.status =
      bytes.size() < r.required_size ? Status::incomplete : Status::complete;
  return r;
}
} // namespace luv::wire
