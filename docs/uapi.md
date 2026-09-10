# VNPU Userspace UAPI

Status: implemented UAPI contract under Revision B validation.

This document defines the userspace ABI between the Linux VNPU driver and
applications. The UAPI is implemented by the guest Linux kernel driver and is
exposed through a character device.

## Driver Identity

| Item | Value |
|---|---|
| Kernel module | `vnpu_drv` |
| Device node | `/dev/vnpu0` |
| Class name | `vnpu` |
| UAPI header | `linux-driver/include/vnpu_uapi.h` |

The driver binds to the QEMU VNPU PCI device:

| Field | Value |
|---|---:|
| PCI vendor ID | `0x1B36` |
| PCI device ID | `0x1000` |
| Supported revision in MVP | Revision A (`1`) and Revision B (`2`) |

## ABI Rules

- All UAPI structures use fixed-width Linux integer types such as `__u32`,
  `__u64`, `__s8`, and `__s32`.
- Every userspace-visible structure includes `abi_version`.
- The initial ABI version is `1`.
- The MVP supports one outstanding operation per device.
- The driver must validate all userspace input before writing MMIO registers.
- The driver must not expose raw MMIO access to userspace.
- The driver must return bounded errors instead of waiting forever.
- Device operation timeouts are valid from `125` ms through `1000` ms,
  inclusive.

## Ioctl Commands

The ioctl set is:

```c
#define VNPU_IOCTL_GET_INFO   _IOR(VNPU_IOCTL_MAGIC, 0x00, struct vnpu_info)
#define VNPU_IOCTL_RUN_DOT    _IOWR(VNPU_IOCTL_MAGIC, 0x01, struct vnpu_dot_request)
#define VNPU_IOCTL_RESET      _IO(VNPU_IOCTL_MAGIC, 0x02)
#define VNPU_IOCTL_SET_FAULT  _IOW(VNPU_IOCTL_MAGIC, 0x03, struct vnpu_fault_request)
#define VNPU_IOCTL_GET_STAT   _IOR(VNPU_IOCTL_MAGIC, 0x04, struct vnpu_status)
```

The exact command numbers are defined in `linux-driver/include/vnpu_uapi.h`.
Changing command numbers or structure layout requires a UAPI documentation
update and ABI compatibility reasoning.

## Data Structures

### `struct vnpu_info`

```c
struct vnpu_info {
    __u32 abi_version;
    __u32 device_id;
    __u32 revision;
    __u32 capabilities;
};
```

Returned by `VNPU_IOCTL_GET_INFO`.

| Field | Meaning |
|---|---|
| `abi_version` | UAPI version returned by the driver |
| `device_id` | MMIO `DEVICE_ID`, expected `0x564E5055` |
| `revision` | MMIO `REVISION`; Revision A is `1`, Revision B is `2` |
| `capabilities` | MMIO `CAPABILITIES`; Revision A reports `0`, Revision B reports `0x0000007D` |

### `struct vnpu_dot_request`

```c
struct vnpu_dot_request {
    __u32 abi_version;
    __u32 job_id;
    __u32 vector_length;
    __u32 timeout_ms;
    __u32  input_a[4];
    __u32  input_b[4];
    __s32 result;
    __s32 driver_status;
    __u32 device_error;
};
```

Used by `VNPU_IOCTL_RUN_DOT`.

| Field | Direction | Meaning |
|---|---|---|
| `abi_version` | in | Must be `1` |
| `job_id` | in | Reserved for Revision B; ignored for Revision A |
| `vector_length` | in | Revision A supports `8` only; Revision B supports `8` and `16` |
| `timeout_ms` | in | Maximum wait time for completion; valid range is `125` through `1000` ms, inclusive |
| `input_a` | in | Packed signed INT8 vector A words |
| `input_b` | in | Packed signed INT8 vector B words |
| `result` | out | Signed INT32 dot-product result |
| `driver_status` | out | Driver-level result code |
| `device_error` | out | MMIO `ERROR_CODE` on device failure |

The current UAPI shape mirrors the MMIO packing used by the device model:
`input_a[0..3]` and `input_b[0..3]` are 32-bit words, each containing four
packed signed INT8 elements in little-endian byte order.

For Revision A, only `input_a[0..1]` and `input_b[0..1]` are consumed, covering
elements `0..7`. For Revision B length `8`, the same lower two words are
consumed and the upper input words are ignored. For Revision B length `16`, all
four packed input words are consumed. Userspace should set unused upper words to
zero for deterministic logs and future compatibility.

### Device Timeout Range

`timeout_ms` is the device operation timeout in milliseconds. Valid userspace
requests must set:

```text
125 <= timeout_ms <= 1000
```

Values below `125` ms or above `1000` ms must be rejected as invalid arguments.

### `struct vnpu_fault_request`

```c
struct vnpu_fault_request {
    __u32 abi_version;
    __u32 fault_mask;
};
```

Used by `VNPU_IOCTL_SET_FAULT`.

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `VNPU_FAULT_IRQ_DROP` | Complete without raising an IRQ |
| 1 | `VNPU_FAULT_STUCK_BUSY` | Leave device busy until reset |
| 2 | `VNPU_FAULT_CORRUPT_RESULT` | Deterministically alter the result |
| 3 | `VNPU_FAULT_FORCE_ERROR` | Complete with device error |

The device model accepts only one active fault bit. If multiple bits are set in
raw MMIO, the lowest-numbered bit is selected by the device. The driver UAPI
rejects multi-bit masks before writing `FAULT_CONTROL`.

Bits `4..31` are reserved. The driver UAPI rejects them, and userspace must
write them as zero.

### `struct vnpu_status`

```c
struct vnpu_status {
    __u32 abi_version;
    __u64 submitted;
    __u64 completed;
    __u64 timed_out;
    __u64 device_error;
    __u64 resets;
};
```

Returned by `VNPU_IOCTL_GET_STAT`.

| Field | Meaning |
|---|---|
| `submitted` | Number of accepted `RUN_DOT` submissions |
| `completed` | Number of successful completions |
| `timed_out` | Number of timeout paths |
| `device_error` | Number of device-reported failures |
| `resets` | Number of driver-initiated resets |

## Reserved Device Fields

The current `qemu-device/src/vnpu.c` implementation contains reserved registers
and revision-specific behavior. The driver must preserve these semantics
instead of inventing behavior that the device model does not implement.

| Device field | MMIO offset / UAPI field | Current behavior | Driver/UAPI rule |
|---|---:|---|---|
| `CAPABILITIES` | `0x008` / `vnpu_info.capabilities` | Revision A reads `0`; Revision B must expose the documented feature bitmap | Accept `0` for Revision A; require `0x0000007D` bits for Revision B |
| `JOB_ID` | `0x020` / `vnpu_dot_request.job_id` | Revision A reads `0` and ignores writes; Revision B preserves the written value | Ignore the field for Revision A; program it for Revision B when provided |
| Revision B length | `VECTOR_LENGTH == 8` or `16` | Revision B accepts both length `8` and length `16`; Revision A rejects length `16` with `VNPU_ERR_INVALID_LENGTH` | Allow `8` for Revision A; allow `8` and `16` for Revision B |
| Upper input words | `input_a[2..3]`, `input_b[2..3]` | Stored by MMIO; consumed only for length `16` | Require userspace to zero them for length `8`; include them in result calculation for length `16` |
| Fault mask upper bits | `fault_mask[31:4]` | Device masks unsupported bits off | Driver must mask or reject unsupported bits; userspace must write zero |
| Unimplemented MMIO offsets | BAR0 offsets outside the documented register map | Reads return zero and writes have no effect in the current model | Driver must not expose raw access to these offsets |

The PCI configuration-space revision currently differs from the MMIO
`REVISION` register: the QEMU class sets PCI revision `0x10`, while the MMIO
`REVISION` register returns `1` for Revision A and `2` for Revision B. Driver
revision handling must use the MMIO `REVISION` register for hardware revision
decisions.

## Driver Status Values

`driver_status` in `struct vnpu_dot_request` reports driver-level completion.
The numeric values are defined in `linux-driver/include/vnpu_uapi.h`:

| Name | Meaning |
|---|---|
| `DRIVER_STATUS_OK` | Operation completed and `result` is valid |
| `DRIVER_STATUS_TIMEOUT` | Completion did not arrive before `timeout_ms` |
| `DRIVER_STATUS_DEVICE_ERROR` | Device entered `STATUS_ERROR` |
| `DRIVER_STATUS_INV_REV` | Invalid ABI or unsupported revision |
| `DRIVER_STATUS_INV_LEN` | Unsupported vector length |
| `DRIVER_STATUS_INV_TIME` | Invalid timeout value |
| `DRIVER_STATUS_INV_MULTIMASK` | Invalid multi-bit fault mask |
| `DRIVER_STATUS_INV_BIT` | Invalid fault bit |

Kernel syscall return values still use normal Linux error codes. For example,
invalid userspace pointers return `-EFAULT`, bad ABI versions return `-EINVAL`,
and unsupported operations return `-EOPNOTSUPP` or `-ENODEV` depending on the
failure point.

## `VNPU_IOCTL_GET_INFO`

Purpose: return stable device identity and UAPI version.

Driver behavior:

1. Validate the userspace pointer.
2. Read cached probe-time identity values.
3. Copy `struct vnpu_info` to userspace.

This ioctl must not start hardware work.

## `VNPU_IOCTL_RUN_DOT`

Purpose: submit one signed INT8 dot-product operation and wait for completion.

Driver behavior:

1. Copy `struct vnpu_dot_request` from userspace.
2. Validate `abi_version`.
3. Validate `vector_length` against the probed hardware revision.
4. Validate `timeout_ms` is within the inclusive `125` to `1000` ms device
   timeout range.
5. Acquire the device submission mutex.
6. Clear stale completion state.
7. Program packed input registers and `VECTOR_LENGTH`.
8. Enable completion/error IRQs.
9. Write `CONTROL_START`.
10. Wait for IRQ-driven completion with timeout.
11. On success, read `RESULT` and clear/acknowledge IRQ status.
12. On timeout or stuck busy, issue `CONTROL_RESET` and report timeout.
13. Copy the updated request back to userspace.

The MVP supports one active request at a time.

## `VNPU_IOCTL_RESET`

Purpose: reset runtime device state.

Driver behavior:

1. Acquire the device submission mutex.
2. Write `CONTROL_RESET`.
3. Clear driver completion state.
4. Increment reset stats.

This ioctl is valid even after timeout or device error.

## `VNPU_IOCTL_SET_FAULT`

Purpose: configure deterministic runtime fault injection for validation.

Driver behavior:

1. Copy `struct vnpu_fault_request` from userspace.
2. Validate `abi_version`.
3. Reject multi-bit masks and bits outside `0..3`.
4. Write `FAULT_CONTROL`.

This ioctl is test-only behavior and must not be used to imply production
hardware behavior.

## `VNPU_IOCTL_GET_STAT`

Purpose: return driver-maintained counters for validation and debugging.

Driver behavior:

1. Snapshot counters safely.
2. Copy `struct vnpu_status` to userspace.

Stats are diagnostic counters, not a stable performance interface.

## Safety Requirements

The driver must:

- validate `abi_version`;
- use `copy_from_user()` and `copy_to_user()`;
- never dereference userspace pointers directly;
- validate vector length before MMIO writes;
- reject unsupported revision values during probe;
- protect command submission with a mutex;
- use a completion object or wait queue for IRQ-driven completion;
- acknowledge `IRQ_STATUS` using RW1C semantics;
- avoid sleeping in the IRQ handler;
- recover or return a clear error after timeout;
- unwind resources consistently in probe failure paths and `remove()`.

## Compatibility Policy

After `linux-driver/include/vnpu_uapi.h` and userspace tests exist, changes to
ioctl numbers, structure layout, field meaning, or ABI version handling
require:

- an update to this document;
- matching driver and userspace test changes;
- a short ADR if compatibility or reproducibility is affected.
