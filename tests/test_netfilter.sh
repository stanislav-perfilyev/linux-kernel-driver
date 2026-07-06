#!/usr/bin/env bash
# test_netfilter.sh — integration tests for netfilter_hook.ko
set -euo pipefail

PASS=0; FAIL=0
ok()   { echo "  PASS: $1"; ((PASS++)); }
fail() { echo "  FAIL: $1"; ((FAIL++)); }

echo "[T1] /proc/netfilter_hook/stats"
[[ -r /proc/netfilter_hook/stats ]] && ok "stats" || fail "missing"

echo "[T2] stats fields"
grep -q "accepted" /proc/netfilter_hook/stats && ok "has accepted" || fail "no accepted"
grep -q "dropped"  /proc/netfilter_hook/stats && ok "has dropped"  || fail "no dropped"

echo "[T3] Add rule"
echo "192.168.99.1:9999" | sudo tee /proc/netfilter_hook/rules > /dev/null
grep -q "192.168.99.1:9999" /proc/netfilter_hook/rules && ok "rule added" || fail "rule not found"

echo "[T4] Flush rules"
echo "flush" | sudo tee /proc/netfilter_hook/rules > /dev/null
LINES=$(wc -l < /proc/netfilter_hook/rules)
[[ "$LINES" -eq 0 ]] && ok "flushed" || fail "rules remain: $LINES"

echo ""
echo "Results: PASS=$PASS FAIL=$FAIL"
[[ $FAIL -eq 0 ]] && exit 0 || exit 1
