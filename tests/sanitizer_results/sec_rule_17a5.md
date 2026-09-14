# SEC 17a-5 arithmetic validation

2026-09-14, local macOS; baseline cdae713 plus the remediation working-tree patch.

Commands:

```sh
cmake --build build --target luv_sec_rule_17a5 --parallel 4
./build/luv_sec_rule_17a5
/opt/homebrew/opt/llvm/bin/clang++ -std=c++20 -O1 -g -Wall -Wextra -Wsign-conversion -fsanitize=undefined -fno-sanitize-recover=all test_sec_rule_17a5.cpp -o /tmp/fort-17a5-ubsan
/tmp/fort-17a5-ubsan
```

Both binaries exited 0 and printed `SEC Rule 17a-5 Broker-Dealer Audit Engine tests
passed.` The standalone compile emitted no warnings; UBSan emitted no diagnostics.
Checks include each unsigned input above INT64_MAX, UINT64_MAX, zero, positive
overflow, negative overflow/INT64_MIN intermediates, and exact large-value ratio
and fee calculations. Negative input is not representable by the schedule fields.

Inputs outside signed-dollar range or overflowing intermediates return an invalid
result with approval flags cleared; callers must inspect `arithmetic_valid`.
The fix preserves the model's existing rounding policies; legal correctness is not
established. Remaining compliance modules have not received equivalent review.

`LUV_ENABLE_SIGN_CONVERSION_WARNINGS` enables the broader warning audit without
claiming the existing repository is free of implicit sign conversions.
