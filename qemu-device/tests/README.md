# VNPU Device Model Tests

These tests validate the QEMU VNPU device model before the Linux kernel driver
exists. Run them inside the Buildroot guest as root after booting QEMU with:

```bash
-device vnpu
```

## devmem smoke test

Requirements:

- BusyBox or standalone `devmem`
- VNPU PCI device visible in `/sys/bus/pci/devices`

Run:

```bash
scripts/vnpu-devmem-smoke.sh
```

The script finds PCI device `1b36:1000`, checks BAR0 size, reads reset
registers, runs a known dot-product operation, and verifies the deterministic
`CORRUPT_RESULT` fault.

## mmap userspace smoke tool

Build for the guest target, then copy the binary into the root filesystem or
guest:

```bash
make -C qemu-device/tests CC=/path/to/x86_64-buildroot-linux-gnu-gcc
```

Run in the guest as root:

```bash
./vnpu-mmio-smoke
```

The tool maps `/sys/bus/pci/devices/<BDF>/resource0` and performs the same MMIO
checks without requiring `devmem`.
