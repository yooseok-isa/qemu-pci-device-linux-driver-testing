#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QEMU_BIN="${QEMU_BIN:-/home/isa_codex/src/buildroot/output/build/host-qemu-10.2.0/build/qemu-system-x86_64}"
KERNEL_IMAGE="${KERNEL_IMAGE:-$REPO_ROOT/buildroot/output/images/bzImage}"
ROOTFS_IMAGE="${ROOTFS_IMAGE:-$REPO_ROOT/buildroot/output/images/rootfs.ext4}"
KERNEL_APPEND='console=ttyS0 root=/dev/vda rw panic=-1 init=/bin/sh -- -c "mkdir -p /mnt/repo; mount -t 9p -o trans=virtio repo /mnt/repo || echo WARNING: repo 9p mount failed; exec /sbin/init"'

"$QEMU_BIN" \
    -M q35 \
    -m 512M \
    -kernel "$KERNEL_IMAGE" \
    -append "$KERNEL_APPEND" \
    -drive "file=$ROOTFS_IMAGE,format=raw,if=virtio" \
	-device vnpu \
	-fsdev "local,id=repo,path=$REPO_ROOT,security_model=none" \
	-device virtio-9p-pci,fsdev=repo,mount_tag=repo \
	-chardev stdio,id=serial0,signal=off \
	-serial chardev:serial0 \
    -display none \
    -no-reboot
