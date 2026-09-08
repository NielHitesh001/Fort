#pragma once

#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace luv {

class CrashDiagnosticsHandler {
public:
    static void install() noexcept {
        struct sigaction sa{};
        sa.sa_sigaction = &CrashDiagnosticsHandler::handle_signal;
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
        sigemptyset(&sa.sa_mask);

        (void)::sigaction(SIGSEGV, &sa, nullptr);
        (void)::sigaction(SIGABRT, &sa, nullptr);
        (void)::sigaction(SIGBUS, &sa, nullptr);
        (void)::sigaction(SIGFPE, &sa, nullptr);
        (void)::sigaction(SIGILL, &sa, nullptr);
    }

    static void set_dump_path(const char* path) noexcept {
        if (path) {
            std::strncpy(_dump_path, path, sizeof(_dump_path) - 1);
            _dump_path[sizeof(_dump_path) - 1] = '\0';
        }
    }

    static void set_state_context(uint64_t last_seq, uint32_t active_orders, bool cb_tripped) noexcept {
        _last_sequence = last_seq;
        _active_orders = active_orders;
        _circuit_breaker_tripped = cb_tripped;
    }

    static void write_crash_dump(int sig, siginfo_t* info = nullptr) noexcept {
        int fd = ::open(_dump_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0) return;

        safe_write(fd, "{\n  \"event\": \"CRASH_DIAGNOSTICS\",\n");
        safe_write(fd, "  \"signal\": ");
        safe_write_int(fd, sig);
        safe_write(fd, ",\n  \"fault_addr\": ");
        if (info) {
            safe_write_hex(fd, reinterpret_cast<uintptr_t>(info->si_addr));
        } else {
            safe_write(fd, "0");
        }
        safe_write(fd, ",\n  \"last_sequence\": ");
        safe_write_uint(fd, _last_sequence);
        safe_write(fd, ",\n  \"active_orders\": ");
        safe_write_uint(fd, _active_orders);
        safe_write(fd, ",\n  \"circuit_breaker_tripped\": ");
        safe_write(fd, _circuit_breaker_tripped ? "true\n}\n" : "false\n}\n");

        (void)::fsync(fd);
        (void)::close(fd);
    }

private:
    static void handle_signal(int sig, siginfo_t* info, void*) noexcept {
        write_crash_dump(sig, info);
        // Default behavior will now re-execute and trigger core dump
        (void)::raise(sig);
    }

    static void safe_write(int fd, const char* str) noexcept {
        if (!str) return;
        size_t len = 0;
        while (str[len] != '\0') ++len;
        (void)::write(fd, str, len);
    }

    static void safe_write_int(int fd, int64_t val) noexcept {
        char buf[32];
        int pos = 0;
        if (val < 0) {
            buf[pos++] = '-';
            val = -val;
        }
        char temp[32];
        int tpos = 0;
        if (val == 0) {
            temp[tpos++] = '0';
        } else {
            while (val > 0) {
                temp[tpos++] = static_cast<char>('0' + (val % 10));
                val /= 10;
            }
        }
        while (tpos > 0) {
            buf[pos++] = temp[--tpos];
        }
        buf[pos] = '\0';
        safe_write(fd, buf);
    }

    static void safe_write_uint(int fd, uint64_t val) noexcept {
        safe_write_int(fd, static_cast<int64_t>(val));
    }

    static void safe_write_hex(int fd, uintptr_t val) noexcept {
        char buf[32];
        buf[0] = '0';
        buf[1] = 'x';
        int pos = 2;
        char temp[32];
        int tpos = 0;
        if (val == 0) {
            temp[tpos++] = '0';
        } else {
            constexpr const char* hex_digits = "0123456789abcdef";
            while (val > 0) {
                temp[tpos++] = hex_digits[val & 0xF];
                val >>= 4;
            }
        }
        while (tpos > 0) {
            buf[pos++] = temp[--tpos];
        }
        buf[pos] = '\0';
        safe_write(fd, buf);
    }

    inline static char _dump_path[128] = "luv_crash_dump.json";
    inline static uint64_t _last_sequence = 0;
    inline static uint32_t _active_orders = 0;
    inline static bool _circuit_breaker_tripped = false;
};

} // namespace luv
