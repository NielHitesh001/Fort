#include "luv_fix.hpp"
#include <cassert>
#include <cstring>

int main() {
    luv::fix::FixMessageView view;
    const char* messages[] = {
        "8=FIX.4.4|38=9223372036854775807|",
        "8=FIX.4.4|38=-9223372036854775808|",
        "8=FIX.4.4|38=9223372036854775808|",
        "8=FIX.4.4|38=-9223372036854775809|",
        "8=FIX.4.4|38=18446744073709551615|",
        "8=FIX.4.4|38=-|", "8=FIX.4.4|38=12garbage|", "8=FIX.4.4|38=0|"};
    const int64_t expected[] = {INT64_MAX, INT64_MIN, 99, 99, 99, 99, 99, 0};
    for (size_t i = 0; i < 8; ++i) {
        assert(view.parse(messages[i], std::strlen(messages[i])));
        assert(view.get_int(38, 99) == expected[i]);
    }
    constexpr char oversized[] = "8=FIX.4.4|4294967334=5|";
    assert(!view.parse(oversized, sizeof(oversized) - 1));
}
