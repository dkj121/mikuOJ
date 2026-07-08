#!/usr/bin/env bash
# 6 种语言 A+B AC 测试（工具链缺失的语言自动跳过）
set -uo pipefail
BIN="${CPPJUDGE:-./build/cppjudge}"
SB="${CPPJUDGE_SANDBOX:-auto}"
PASS=0; FAIL=0; SKIP=0

run() {
    local name="$1" sub="$2" tool="$3"
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "  [$name] SKIP (no $tool)"; SKIP=$((SKIP + 1)); return
    fi
    OUT=$("$BIN" judge --problem problems/A+B --sandbox-type "$SB" --submission "$sub" 2>/dev/null || true)
    if echo "$OUT" | grep -q '"final_verdict":"Accepted"'; then
        echo "  [$name] PASS"; PASS=$((PASS + 1))
    else
        echo "  [$name] FAIL: $OUT"; FAIL=$((FAIL + 1))
    fi
}

echo "=== multi-language AC ==="
run "C++"     submissions/cpp/solution.cpp      g++
run "C"       submissions/c/solution.c          gcc
run "Python3" submissions/python3/solution.py   python3
run "Java"    submissions/java/solution.java     javac
run "Go"      submissions/go/solution.go         go
run "Rust"    submissions/rust/solution.rs       rustc
echo "=== $PASS passed, $FAIL failed, $SKIP skipped ==="
exit $FAIL
