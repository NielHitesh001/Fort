#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

failed=0
check() {
    label=$1
    shift
    if "$@"; then
        printf 'PASS: %s\n' "$label"
    else
        printf 'FAIL: %s\n' "$label"
        failed=1
    fi
}

printf '%s\n' '=== PHASE A: GATE A VERIFICATION ==='
check 'git worktree is clean' test -z "$(git status --porcelain)"
check 'crash recovery harness exists and is executable' test -x build-day4/crash_recovery_harness
check 'staging soak runner exists and is executable' test -x build-phase2/staging_soak_runner
check 'evidence archive exists' test -s docs/GATE_CLOSURE_EVIDENCE.md
check 'staging readiness file exists' test -s docs/STAGING_ENVIRONMENT_READINESS.md

if test -s docs/STAGING_ENVIRONMENT_READINESS.md && \
    grep -Fq 'Status: **VERIFIED**' docs/STAGING_ENVIRONMENT_READINESS.md && \
   ! grep -q '^-[[:space:]]*\[ \]' docs/STAGING_ENVIRONMENT_READINESS.md; then
    printf '%s\n' 'PASS: staging readiness is explicitly verified'
else
    printf '%s\n' 'FAIL: staging readiness is not explicitly verified'
    failed=1
fi

if test "$failed" -eq 0; then
    printf '%s\n' 'GATE A: PASS'
    exit 0
fi

printf '%s\n' 'GATE A: PENDING'
exit 1
