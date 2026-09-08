#include "luv_cat.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string_view>

void test_cat_meno_format() {
    char buf[512];
    size_t len = luv::cat::CatReportFormatter::format_meno(
        buf, sizeof(buf),
        "CRID_1234",
        987654321,
        "AAPL",
        1725753600000000000ULL,
        'B',
        15025,
        500,
        "LMT"
    );

    assert(len > 0);
    assert(std::strstr(buf, "\"type\":\"MENO\""));
    assert(std::strstr(buf, "\"catReporterIMID\":\"CRID_1234\""));
    assert(std::strstr(buf, "\"orderID\":\"987654321\""));
    assert(std::strstr(buf, "\"symbol\":\"AAPL\""));
    assert(std::strstr(buf, "\"side\":\"B\""));
    assert(std::strstr(buf, "\"price\":15025"));
    assert(std::strstr(buf, "\"quantity\":500"));

    std::printf("[PASS] test_cat_meno_format\n");
}

void test_cat_meot_format() {
    char buf[512];
    size_t len = luv::cat::CatReportFormatter::format_meot(
        buf, sizeof(buf),
        "CRID_1234",
        987654321,
        11223344,
        "AAPL",
        1725753600000000000ULL,
        'B',
        15025,
        200,
        300
    );

    assert(len > 0);
    assert(std::strstr(buf, "\"type\":\"MEOT\""));
    assert(std::strstr(buf, "\"execID\":\"11223344\""));
    assert(std::strstr(buf, "\"quantity\":200"));
    assert(std::strstr(buf, "\"leavesQty\":300"));

    std::printf("[PASS] test_cat_meot_format\n");
}

void test_cat_meoc_format() {
    char buf[512];
    size_t len = luv::cat::CatReportFormatter::format_meoc(
        buf, sizeof(buf),
        "CRID_1234",
        987654321,
        "AAPL",
        1725753600000000000ULL,
        'B',
        300,
        0
    );

    assert(len > 0);
    assert(std::strstr(buf, "\"type\":\"MEOC\""));
    assert(std::strstr(buf, "\"cancelQty\":300"));
    assert(std::strstr(buf, "\"leavesQty\":0"));

    std::printf("[PASS] test_cat_meoc_format\n");
}

int main() {
    test_cat_meno_format();
    test_cat_meot_format();
    test_cat_meoc_format();
    std::printf("All SEC Rule 613 CAT reporting tests passed successfully.\n");
    return 0;
}
