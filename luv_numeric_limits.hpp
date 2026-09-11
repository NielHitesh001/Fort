#pragma once

#include <cstdint>
#include <limits>

namespace luv::numeric {

// Market-data quantities and the OUCH price field are bounded deliberately.
// Keeping these limits in one place prevents an upstream decoder, the LOB,
// and the order gateway from accepting incompatible values.
inline constexpr int64_t kMinPrice = 1;
inline constexpr int64_t kMaxPrice = 0xFFFF'FFFFLL;
inline constexpr int64_t kMinQuantity = 1;
inline constexpr int64_t kMaxQuantity = 1'000'000'000LL;

[[nodiscard]] constexpr bool is_valid_price(int64_t value) noexcept {
    return value >= kMinPrice && value <= kMaxPrice;
}

[[nodiscard]] constexpr bool is_valid_quantity(int64_t value) noexcept {
    return value >= kMinQuantity && value <= kMaxQuantity;
}

[[nodiscard]] constexpr bool checked_add(int64_t lhs, int64_t rhs,
                                         int64_t& result) noexcept {
    if ((rhs > 0 && lhs > std::numeric_limits<int64_t>::max() - rhs) ||
        (rhs < 0 && lhs < std::numeric_limits<int64_t>::min() - rhs))
        return false;
    result = lhs + rhs;
    return true;
}

}  // namespace luv::numeric
