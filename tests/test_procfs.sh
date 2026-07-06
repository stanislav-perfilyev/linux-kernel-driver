#!/usr/bin/env bash
# test_procfs.sh — integration tests for procfs_driver.ko
set -euo pipefail

PASS=0; FAIL=0
ok()   { echo "  PASS: $1"; ((PASS++)); }
fail() { echo "  FAIL: $1"; ((FAIL++)); }

echo "[T1] /proc/mydriver/stats exists"
[[ -r /proc/mydriver/stats ]] && ok "stats" || fail "missing"

echo "[T2] stats contains uptime_sec"
grep -q "uptime_sec" /proc/mydriver/stats && ok "has uptime_sec" || fail "no uptime_sec"

echo "[T3] config read"
[[ -r /proc/mydriver/config ]] && ok "config readable" || fail "config missing"

echo "[T4] config write log_level=2"
echo "log_level=2" | sudo tee /proc/mydriver/config > /dev/null
grep -q "log_level=2" /proc/mydriver/config && ok "log_level=2" || fail "not updated"

echo "[T5] restore log_level=1"
echo "log_level=1" | sudo tee /proc/mydriver/config > /dev/null

echo ""
echo "Results: PASS=$PASS FAIL=$FAIL"
[[ $FAIL -eq 0 ]] && exit 0 || exit 1
