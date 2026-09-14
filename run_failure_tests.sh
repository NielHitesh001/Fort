#!/bin/sh
set -eu
cmake -S . -B build
cmake --build build --target luv_feed_disconnect_partial_fill luv_arena_exhaustion_recovery --parallel 4
ctest --test-dir build -R '^(feed_disconnect_partial_fill|arena_exhaustion_recovery)$' --output-on-failure
