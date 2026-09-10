#!/usr/bin/env bash
set -euo pipefail

VNPU_VENDOR="${VNPU_VENDOR:-0x1b36}"
VNPU_DEVICE="${VNPU_DEVICE:-0x1000}"
DEVMEM="${DEVMEM:-devmem}"

REG_DEVICE_ID=0x000
REG_REVISION=0x004
REG_CAPABILITIES=0x008
REG_CONTROL=0x00c
REG_STATUS=0x010
REG_ERROR_CODE=0x01c
REG_VECTOR_LENGTH=0x024
REG_INPUT_A0=0x100
REG_INPUT_A1=0x104
REG_INPUT_B0=0x120
REG_INPUT_B1=0x124
REG_RESULT=0x140
REG_FAULT_CONTROL=0x180

CONTROL_START=0x1
CONTROL_RESET=0x2

STATUS_IDLE=0
STATUS_DONE=2
STATUS_ERROR=3

FAULT_CORRUPT_RESULT=0x4

die() {
    echo "error: $*" >&2
    exit 1
}

norm_hex() {
    local value="$1"
    value="${value%% *}"
    value="${value//$'\r'/}"
    value="${value//$'\n'/}"
    printf '0x%x' "$((value))"
}

find_vnpu_bdf() {
    local dev vendor device

    for dev in /sys/bus/pci/devices/*; do
        [ -r "$dev/vendor" ] || continue
        [ -r "$dev/device" ] || continue
        vendor="$(tr 'A-F' 'a-f' < "$dev/vendor")"
        device="$(tr 'A-F' 'a-f' < "$dev/device")"
        if [ "$vendor" = "$VNPU_VENDOR" ] && [ "$device" = "$VNPU_DEVICE" ]; then
            basename "$dev"
            return 0
        fi
    done

    return 1
}

mmio_addr() {
    local offset="$1"
    printf '0x%x' "$((BAR0_START + offset))"
}

mmio_read() {
    "$DEVMEM" "$(mmio_addr "$1")" 32
}

mmio_write() {
    "$DEVMEM" "$(mmio_addr "$1")" 32 "$2" >/dev/null
}

expect_read() {
    local offset="$1"
    local expected="$2"
    local label="$3"
    local actual

    actual="$(norm_hex "$(mmio_read "$offset")")"
    expected="$(norm_hex "$expected")"
    if [ "$actual" != "$expected" ]; then
        die "$label: expected $expected, got $actual"
    fi

    echo "ok: $label = $actual"
}

wait_done() {
    local status
    local i

    for i in $(seq 1 50); do
        status="$(( $(mmio_read "$REG_STATUS") ))"
        case "$status" in
            "$STATUS_DONE")
                return 0
                ;;
            "$STATUS_ERROR")
                die "device entered STATUS_ERROR, error_code=$(mmio_read "$REG_ERROR_CODE")"
                ;;
        esac
        if command -v usleep >/dev/null 2>&1; then
            usleep 20000
        else
            sleep 1
        fi
    done

    die "timed out waiting for STATUS_DONE"
}

run_dot_once() {
    local expected="$1"
    local label="$2"
    local result

    mmio_write "$REG_VECTOR_LENGTH" 0x8
    mmio_write "$REG_INPUT_A0" 0x04030201
    mmio_write "$REG_INPUT_A1" 0x08070605
    mmio_write "$REG_INPUT_B0" 0x01010101
    mmio_write "$REG_INPUT_B1" 0x01010101
    mmio_write "$REG_CONTROL" "$CONTROL_START"

    wait_done

    result="$(norm_hex "$(mmio_read "$REG_RESULT")")"
    expected="$(norm_hex "$expected")"
    if [ "$result" != "$expected" ]; then
        die "$label: expected result $expected, got $result"
    fi

    echo "ok: $label result = $result"
}

command -v "$DEVMEM" >/dev/null 2>&1 || die "devmem command not found"
[ "$(id -u)" -eq 0 ] || die "must run as root to access MMIO"

BDF="$(find_vnpu_bdf)" || die "VNPU PCI device $VNPU_VENDOR:$VNPU_DEVICE not found"
RESOURCE="/sys/bus/pci/devices/$BDF/resource"
[ -r "$RESOURCE" ] || die "resource file not readable: $RESOURCE"

read -r BAR0_START_HEX BAR0_END_HEX _ < "$RESOURCE"
BAR0_START="$((BAR0_START_HEX))"
BAR0_END="$((BAR0_END_HEX))"
BAR0_SIZE="$((BAR0_END - BAR0_START + 1))"

[ "$BAR0_SIZE" -eq 4096 ] || die "BAR0 size expected 4096, got $BAR0_SIZE"

echo "VNPU BDF: $BDF"
printf 'BAR0: 0x%x-0x%x (%u bytes)\n' "$BAR0_START" "$BAR0_END" "$BAR0_SIZE"

mmio_write "$REG_CONTROL" "$CONTROL_RESET"

expect_read "$REG_DEVICE_ID" 0x564e5055 "DEVICE_ID"
expect_read "$REG_REVISION" 0x2 "REVISION"
expect_read "$REG_CAPABILITIES" 0x7D "CAPABILITIES"
expect_read "$REG_STATUS" "$STATUS_IDLE" "STATUS after reset"
expect_read "$REG_VECTOR_LENGTH" 0x10 "VECTOR_LENGTH after reset"

run_dot_once 0x24 "dot8"

mmio_write "$REG_CONTROL" "$CONTROL_RESET"
mmio_write "$REG_FAULT_CONTROL" "$FAULT_CORRUPT_RESULT"
run_dot_once 0x25 "corrupt-result fault"

mmio_write "$REG_CONTROL" "$CONTROL_RESET"

echo "PASS: VNPU devmem smoke test"
