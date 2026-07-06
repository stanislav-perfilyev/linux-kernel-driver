#!/usr/bin/env bash
# test_chardev.sh — integration tests for chardev.ko
# Requires: module loaded, /dev/mymonitor accessible
set -euo pipefail

DEV="/dev/mymonitor"
PASS=0; FAIL=0

ok()   { echo "  PASS: $1"; ((PASS++)); }
fail() { echo "  FAIL: $1"; ((FAIL++)); }

require_root() {
    [[ "$(id -u)" -eq 0 ]] || { echo "Need root for modprobe"; exit 77; }
}

# ---- Test 1: device exists ----
echo "[T1] Device node"
[[ -c "$DEV" ]] && ok "$DEV is char device" || fail "$DEV missing"

# ---- Test 2: write + read ----
echo "[T2] Write/read round-trip"
MSG="test-$(date +%s)"
echo -n "$MSG" > "$DEV"
GOT=$(cat "$DEV")
[[ "$GOT" == "$MSG" ]] && ok "round-trip" || fail "got='$GOT' want='$MSG'"

# ---- Test 3: ioctl via userspace client ----
echo "[T3] Userspace client"
if [[ -x ./build/chardev_client ]]; then
    ./build/chardev_client "$DEV" && ok "client passed" || fail "client failed"
else
    echo "  SKIP: build/chardev_client not built"
fi

# ---- Summary ----
echo ""
echo "Results: PASS=$PASS FAIL=$FAIL"
[[ $FAIL -eq 0 ]] && exit 0 || exit 1
