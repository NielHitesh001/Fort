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

[[nodiscard]] constexpr bool checked_sub(int64_t lhs, int64_t rhs,
                                         int64_t& result) noexcept {
    if ((rhs < 0 && lhs > std::numeric_limits<int64_t>::max() + rhs) ||
        (rhs > 0 && lhs < std::numeric_limits<int64_t>::min() + rhs))
        return false;
    result = lhs - rhs;
    return true;
}

[[nodiscard]] constexpr bool checked_mul(int64_t lhs, int64_t rhs,
                                         int64_t& result) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return !__builtin_mul_overflow(lhs, rhs, &result);
#else
    if (lhs == 0 || rhs == 0) {
        result = 0;
        return true;
    }
    if (lhs > 0) {
        if (rhs > 0) {
            if (lhs > std::numeric_limits<int64_t>::max() / rhs) return false;
        } else {
            if (rhs < std::numeric_limits<int64_t>::min() / lhs) return false;
        }
    } else {
        if (rhs > 0) {
            if (lhs < std::numeric_limits<int64_t>::min() / rhs) return false;
        } else {
            if (lhs != 0 && rhs < std::numeric_limits<int64_t>::max() / lhs) return false;
        }
    }
    result = lhs * rhs;
    return true;
#endif
}

}  // namespace luv::numeric
