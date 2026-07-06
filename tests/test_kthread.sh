#!/usr/bin/env bash
# test_kthread.sh — integration tests for kthread_monitor.ko
set -euo pipefail

PASS=0; FAIL=0
ok()   { echo "  PASS: $1"; ((PASS++)); }
fail() { echo "  FAIL: $1"; ((FAIL++)); }

echo "[T1] /proc/kmon/snapshot"
[[ -r /proc/kmon/snapshot ]] && ok "snapshot" || fail "missing"

echo "[T2] snapshot fields"
grep -q "total_ram_kb" /proc/kmon/snapshot && ok "has total_ram_kb" || fail "missing"
grep -q "snap_count"   /proc/kmon/snapshot && ok "has snap_count"   || fail "missing"

echo "[T3] total_ram > 0"
VAL=$(grep "total_ram_kb" /proc/kmon/snapshot | awk '{print $3}')
[[ "${VAL:-0}" -gt 0 ]] && ok "total_ram_kb=$VAL" || fail "zero or missing"

echo "[T4] Set interval=500"
echo "interval=500" | sudo tee /proc/kmon/control > /dev/null
sleep 0.6
C1=$(grep "snap_count" /proc/kmon/snapshot | awk '{print $3}')
sleep 0.6
C2=$(grep "snap_count" /proc/kmon/snapshot | awk '{print $3}')
[[ "${C2:-0}" -gt "${C1:-0}" ]] && ok "snap_count incremented" || fail "no increment"

echo "[T5] Stop/start"
echo "stop"  | sudo tee /proc/kmon/control > /dev/null
echo "start" | sudo tee /proc/kmon/control > /dev/null

# Restore
echo "interval=1000" | sudo tee /proc/kmon/control > /dev/null

echo ""
echo "Results: PASS=$PASS FAIL=$FAIL"
[[ $FAIL -eq 0 ]] && exit 0 || exit 1
