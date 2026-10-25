#!/bin/sh
# Boots the integration-test ISO in QEMU with a fresh disposable FihFS disk and
# checks the serial log. Usage: tests/run-ktest.sh BUILD_DIR
set -eu
B=${1:-build}
QEMU=${QEMU:-qemu-system-i386}
DISK="$B/test/disk.img"
LOG="$B/test/serial.log"
rm -f "$DISK" "$LOG"
"$B/host/fihfs-tool" mkfs "$DISK" 8 ktest
# Seed a file so the test can verify data written by the host tool.
printf 'seeded by host\n' > "$B/test/seed.txt"
"$B/host/fihfs-tool" put "$DISK" "$B/test/seed.txt" /seed.txt

run() {
    perl -e "alarm shift; exec @ARGV" 120 "$QEMU" -m 128M -cdrom "$B/test/fihcore-test.iso" -boot d \
        -drive file="$DISK",format=raw,if=ide,index=0 \
        -serial file:"$1" -display none -no-reboot -monitor none || true
}
run "$LOG"
if ! grep -q "KTEST RESULT: PASS" "$LOG"; then
    grep -E "not ok|PANIC|RESULT" "$LOG" || tail -20 "$LOG"
    echo "ktest: FAILED (see $LOG)"
    exit 1
fi
# Second boot: data written in the first run must still be there.
run "$LOG.2"
if ! grep -q "KTEST PERSIST: PASS" "$LOG.2"; then
    grep -E "not ok|PANIC|PERSIST" "$LOG.2" || tail -20 "$LOG.2"
    echo "ktest: persistence check FAILED (see $LOG.2)"
    exit 1
fi
"$B/host/fihfs-tool" fsck "$DISK"
grep -E "^(ok|not ok)" "$LOG" | tail -n 1 >/dev/null
echo "ktest: $(grep -c '^ok' "$LOG") checks passed, persistence verified, fsck clean"
