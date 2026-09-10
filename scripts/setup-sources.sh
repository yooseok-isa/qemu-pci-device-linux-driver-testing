#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BUILDROOT_DIR="${BUILDROOT_DIR:-$HOME/src/buildroot}"
BUILDROOT_REPO="${BUILDROOT_REPO:-https://github.com/buildroot/buildroot.git}"
BUILDROOT_COMMIT="${BUILDROOT_COMMIT:-71d1dddae12e7cbf322d96864d7757f459801f47}"
DEFCONFIG_NAME="vnpu_qemu_x86_64_defconfig"
QEMU_PATCH_NAME="0001-hw-misc-add-vnpu-pci-device.patch"

usage() {
    cat <<EOF
Usage: $(basename "$0") [--no-configure]

Environment:
  BUILDROOT_DIR     Buildroot checkout path. Default: $HOME/src/buildroot
  BUILDROOT_REPO    Buildroot git URL. Default: https://github.com/buildroot/buildroot.git
  BUILDROOT_COMMIT  Pinned Buildroot commit. Default: $BUILDROOT_COMMIT

Options:
  --no-configure    Prepare sources and patches, but do not run make defconfig.
EOF
}

configure=1
while [ "$#" -gt 0 ]; do
    case "$1" in
        --no-configure)
            configure=0
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

if [ "$configure" -eq 1 ]; then
    make -C "$BUILDROOT_DIR" "$DEFCONFIG_NAME"
fi

cat <<EOF
Buildroot sources are ready.

Buildroot path:
  $BUILDROOT_DIR

Pinned commit:
  $BUILDROOT_COMMIT

Installed files:
  configs/$DEFCONFIG_NAME
  package/qemu/$QEMU_PATCH_NAME

Next build commands:
  cd "$BUILDROOT_DIR"
  make -j"\$(nproc)"

After Buildroot finishes, copy artifacts into this repository:
  mkdir -p "$REPO_ROOT/output/images"
  cp "$BUILDROOT_DIR/output/images/bzImage" "$REPO_ROOT/output/images/bzImage"
  cp "$BUILDROOT_DIR/output/images/rootfs.ext2" "$REPO_ROOT/output/images/rootfs.ext4"
EOF
