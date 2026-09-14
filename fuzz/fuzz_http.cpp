#include "luv_wire_parse.hpp"
#include "seed_bytes.hpp"
#include <cstdlib>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  std::array<char, 4096> buffer{};
  size = seed_bytes({reinterpret_cast<const char *>(data), size}, buffer);
  const std::string_view bytes(buffer.data(), size);
  const auto r = luv::wire::request(bytes);
  if (r.status == luv::wire::Status::complete &&
      (r.header_size > r.required_size || r.required_size > size ||
       r.required_size > 2047))
    std::abort();
  // Exercise incremental header/body boundaries without quadratic prefix scans.
  (void)luv::wire::request(bytes.substr(0, size / 2));
  return 0;
}
