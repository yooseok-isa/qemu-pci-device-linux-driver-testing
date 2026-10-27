#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILDROOT_DIR="${BUILDROOT_DIR:-$HOME/src/buildroot}"
BUILDROOT_REPO="${BUILDROOT_REPO:-https://github.com/buildroot/buildroot.git}"
BUILDROOT_COMMIT="${BUILDROOT_COMMIT:-71d1dddae12e7cbf322d96864d7757f459801f47}"
DEFCONFIG_NAME="vnpu_qemu_x86_64_defconfig"
QEMU_PATCH_NAME="0001-hw-misc-add-vnpu-pci-device.patch"
KERNEL_FRAGMENT_NAME="vnpu-linux.fragment"
JOBS="${JOBS:-$(nproc)}"

usage() {
    cat <<EOF
Usage: $(basename "$0") [--configure-only | --no-configure]

By default, prepare sources, configure, build, and copy boot images.

Environment:
  BUILDROOT_DIR     Buildroot checkout path. Default: $HOME/src/buildroot
  BUILDROOT_REPO    Buildroot git URL. Default: https://github.com/buildroot/buildroot.git
  BUILDROOT_COMMIT  Pinned Buildroot commit. Default: $BUILDROOT_COMMIT
  JOBS             Parallel build jobs. Default: $JOBS

Options:
  --configure-only Prepare and configure sources without building or copying images.
  --no-configure   Prepare sources and patches without configuring or building.
EOF
}

configure=1
build=1
while [ "$#" -gt 0 ]; do
    case "$1" in
        --no-configure)
            configure=0
            build=0
            ;;
        --configure-only)
            build=0
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "error: JOBS must be a positive integer" >&2
    exit 2
fi

if [ ! -d "$BUILDROOT_DIR/.git" ]; then
    mkdir -p "$(dirname "$BUILDROOT_DIR")"
    git clone "$BUILDROOT_REPO" "$BUILDROOT_DIR"
fi

git -C "$BUILDROOT_DIR" fetch --tags
git -C "$BUILDROOT_DIR" checkout "$BUILDROOT_COMMIT"

install -D -m 0644 \
    "$REPO_ROOT/buildroot/configs/$DEFCONFIG_NAME" \
    "$BUILDROOT_DIR/configs/$DEFCONFIG_NAME"

install -D -m 0644 \
    "$REPO_ROOT/qemu-device/patches/0001-hw-misc-add-vnpu-pci-device.patch" \
    "$BUILDROOT_DIR/package/qemu/$QEMU_PATCH_NAME"

install -D -m 0644 \
    "$REPO_ROOT/buildroot/configs/$KERNEL_FRAGMENT_NAME" \
    "$BUILDROOT_DIR/board/qemu/x86_64/$KERNEL_FRAGMENT_NAME"

if [ "$configure" -eq 1 ]; then
    make -C "$BUILDROOT_DIR" "$DEFCONFIG_NAME"
fi

if [ "$build" -eq 0 ]; then
    echo "Buildroot sources are ready: $BUILDROOT_DIR"
    exit 0
fi

make -C "$BUILDROOT_DIR" -j"$JOBS"

for artifact in bzImage rootfs.ext2; do
    if [ ! -s "$BUILDROOT_DIR/output/images/$artifact" ]; then
        echo "error: build image is missing or empty: $BUILDROOT_DIR/output/images/$artifact" >&2
        exit 1
    fi
done

IMAGE_DIR="$REPO_ROOT/buildroot/output/images"
mkdir -p "$IMAGE_DIR"
if [ -e "$IMAGE_DIR/rootfs.ext4" ]; then
    backup_dir="$(mktemp -d "$IMAGE_DIR/backup.XXXXXXXX")"
    cp -p "$IMAGE_DIR/rootfs.ext4" "$backup_dir/rootfs.ext4"
    echo "Previous guest filesystem saved: $backup_dir/rootfs.ext4"
fi
cp "$BUILDROOT_DIR/output/images/bzImage" "$IMAGE_DIR/bzImage"
cp "$BUILDROOT_DIR/output/images/rootfs.ext2" "$IMAGE_DIR/rootfs.ext4"

cat <<EOF
Buildroot setup and build are complete.

Buildroot path:
  $BUILDROOT_DIR

Pinned commit:
  $BUILDROOT_COMMIT

Boot images:
  $IMAGE_DIR/bzImage
  $IMAGE_DIR/rootfs.ext4
EOF
