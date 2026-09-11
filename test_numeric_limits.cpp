#include <cassert>
#include <cstdint>
#include <limits>

#include "luv_numeric_limits.hpp"

int main() {
    using namespace luv::numeric;

    assert(is_valid_price(kMinPrice));
    assert(is_valid_price(kMaxPrice));
    assert(!is_valid_price(0));
    assert(!is_valid_price(kMaxPrice + 1));
    assert(is_valid_quantity(kMinQuantity));
    assert(is_valid_quantity(kMaxQuantity));
    assert(!is_valid_quantity(0));
    assert(!is_valid_quantity(-1));
    assert(!is_valid_quantity(std::numeric_limits<int64_t>::max()));

    int64_t result = 0;
    assert(checked_add(10, 20, result) && result == 30);
    assert(!checked_add(std::numeric_limits<int64_t>::max(), 1, result));
    assert(!checked_add(std::numeric_limits<int64_t>::min(), -1, result));
    return 0;
}
