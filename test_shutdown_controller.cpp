#include <cassert>
#include <cstdio>

#include "luv_safety.hpp"

int main() {
    luv::ShutdownController shutdown;
    assert(!shutdown.requested());

    shutdown.clear();
    assert(!shutdown.requested());

    luv::ShutdownController::request_shutdown();
    assert(shutdown.requested());

    shutdown.clear();
    assert(!shutdown.requested());

    std::printf("[OK] shutdown controller lifecycle\n");
    return 0;
}
