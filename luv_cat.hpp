#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace luv {
namespace cat {

// SEC Rule 613 CAT Event Types
enum class CatEventType : uint8_t {
    kMENO = 1, // Member Order Ingestion Event
    kMEOR = 2, // Member Order Route Event
    kMEOT = 3, // Member Order Trade / Execution Event
    kMEOC = 4  // Member Order Cancel Event
};

class CatReportFormatter {
public:
    // Format MENO (Order Ingestion) JSON record
    static size_t format_meno(
        char* out_buf,
        size_t max_len,
        std::string_view reporter_imid,
        uint64_t order_id,
        std::string_view symbol,
        uint64_t event_ts_ns,
        char side, // 'B' or 'S'
        int64_t price,
        int64_t qty,
        std::string_view order_type = "LMT") noexcept
    {
        if (!out_buf || max_len < 256) return 0;

        int written = std::snprintf(
            out_buf, max_len,
            "{"
            "\"actionType\":\"NEW\","
            "\"catReporterIMID\":\"%.*s\","
            "\"type\":\"MENO\","
            "\"orderKeyDate\":\"20260908\","
            "\"orderID\":\"%llu\","
            "\"symbol\":\"%.*s\","
            "\"eventTimestamp\":%llu,"
            "\"manualFlag\":false,"
            "\"side\":\"%c\","
            "\"price\":%lld,"
            "\"quantity\":%lld,"
            "\"orderType\":\"%.*s\","
            "\"timeInForce\":\"DAY\""
            "}",
            static_cast<int>(reporter_imid.size()), reporter_imid.data(),
            static_cast<unsigned long long>(order_id),
            static_cast<int>(symbol.size()), symbol.data(),
            static_cast<unsigned long long>(event_ts_ns),
            side,
            static_cast<long long>(price),
            static_cast<long long>(qty),
            static_cast<int>(order_type.size()), order_type.data()
        );

        return (written > 0 && static_cast<size_t>(written) < max_len) ? static_cast<size_t>(written) : 0;
    }

    // Format MEOT (Order Trade / Fill) JSON record
    static size_t format_meot(
        char* out_buf,
        size_t max_len,
        std::string_view reporter_imid,
        uint64_t order_id,
        uint64_t exec_id,
        std::string_view symbol,
        uint64_t event_ts_ns,
        char side,
        int64_t match_price,
        int64_t match_qty,
        int64_t leaves_qty) noexcept
    {
        if (!out_buf || max_len < 256) return 0;

        int written = std::snprintf(
            out_buf, max_len,
            "{"
            "\"actionType\":\"NEW\","
            "\"catReporterIMID\":\"%.*s\","
            "\"type\":\"MEOT\","
            "\"orderKeyDate\":\"20260908\","
            "\"orderID\":\"%llu\","
            "\"execID\":\"%llu\","
            "\"symbol\":\"%.*s\","
            "\"eventTimestamp\":%llu,"
            "\"side\":\"%c\","
            "\"price\":%lld,"
            "\"quantity\":%lld,"
            "\"leavesQty\":%lld"
            "}",
            static_cast<int>(reporter_imid.size()), reporter_imid.data(),
            static_cast<unsigned long long>(order_id),
            static_cast<unsigned long long>(exec_id),
            static_cast<int>(symbol.size()), symbol.data(),
            static_cast<unsigned long long>(event_ts_ns),
            side,
            static_cast<long long>(match_price),
            static_cast<long long>(match_qty),
            static_cast<long long>(leaves_qty)
        );

        return (written > 0 && static_cast<size_t>(written) < max_len) ? static_cast<size_t>(written) : 0;
    }

    // Format MEOC (Order Cancel) JSON record
    static size_t format_meoc(
        char* out_buf,
        size_t max_len,
        std::string_view reporter_imid,
        uint64_t order_id,
        std::string_view symbol,
        uint64_t event_ts_ns,
        char side,
        int64_t cancel_qty,
        int64_t leaves_qty) noexcept
    {
        if (!out_buf || max_len < 256) return 0;

        int written = std::snprintf(
            out_buf, max_len,
            "{"
            "\"actionType\":\"NEW\","
            "\"catReporterIMID\":\"%.*s\","
            "\"type\":\"MEOC\","
            "\"orderKeyDate\":\"20260908\","
            "\"orderID\":\"%llu\","
            "\"symbol\":\"%.*s\","
            "\"eventTimestamp\":%llu,"
            "\"side\":\"%c\","
            "\"cancelQty\":%lld,"
            "\"leavesQty\":%lld"
            "}",
            static_cast<int>(reporter_imid.size()), reporter_imid.data(),
            static_cast<unsigned long long>(order_id),
            static_cast<int>(symbol.size()), symbol.data(),
            static_cast<unsigned long long>(event_ts_ns),
            side,
            static_cast<long long>(cancel_qty),
            static_cast<long long>(leaves_qty)
        );

        return (written > 0 && static_cast<size_t>(written) < max_len) ? static_cast<size_t>(written) : 0;
    }
};

} // namespace cat
} // namespace luv
