# VNPU Register Map

Status: planned specification, not implemented yet.

This document defines the planned MMIO register contract between the QEMU virtual VNPU PCI device and the Linux driver.

## PCI Device Identity

| Field | Value | Notes |
|---|---:|---|
| Vendor ID | `0x1B36` | Candidate QEMU virtual-device vendor space |
| Device ID | `0x1000` | Candidate local educational device ID |
| BAR | BAR0 | MMIO register window |
| BAR0 size | 4 KiB | `0x1000` bytes |
| Register width | 32-bit | Unless otherwise specified |
| Endianness | little-endian | Guest driver uses 32-bit MMIO accessors |

These IDs are for a local virtual portfolio project. They are not official hardware vendor allocations.

## Register map Summary (BAR0)

| Offset | Register | Access | Reset Value | Description |
|---:|---|---|---:|---|
| `0x000` | `DEVICE_ID` | RO | `0x564E5055` | ASCII-like `VNPU` signature |
| `0x004` | `REVISION` | RO | revision-dependent | `1` for revision A, `2` for revision B |
| `0x008` | `CAPABILITIES` | RO | revision-dependent | Feature bitmap; Revision A reads `0`, Revision B advertises supported features |
| `0x00C` | `CONTROL` | WO/W1S | `0` | Start/reset command register |
| `0x010` | `STATUS` | RO | `STATUS_IDLE` | Device state |
| `0x014` | `IRQ_STATUS` | RW1C | `0` | Pending interrupt bits |
| `0x018` | `IRQ_ENABLE` | RW | `0` | Interrupt enable mask |
| `0x01C` | `ERROR_CODE` | RO | `0` | Last device error |
| `0x020` | `JOB_ID` | RW | `0` | Revision B job identifier |
| `0x024` | `VECTOR_LENGTH` | RW | revision-dependent | Dot-product vector length |
| `0x100`-`0x10C` | `INPUT_A[0..3]` | RW | `0` | 16 packed signed INT8 inputs |
| `0x120`-`0x12C` | `INPUT_B[0..3]` | RW | `0` | 16 packed signed INT8 inputs |
| `0x140` | `RESULT` | RO | `0` | Signed INT32 dot-product result |
| `0x180` | `FAULT_CONTROL` | RW | `0` | Runtime fault-injection bitmap |

Unimplemented offsets return zero. Invalid writes must not crash QEMU.

## Device ID

Offset: `0x000`  
Access: read-only

```text
0x564E5055
```

The value is an ASCII-like signature for `VNPU`. The driver must reject the device if this register does not match.

## Revision

Offset: `0x004`  
Access: read-only

| Value | Meaning |
|---:|---|
| `1` | Revision A |
| `2` | Revision B |

Unsupported revision values must cause the driver probe path to fail clearly.

## Capabilities

Offset: `0x008`  
Access: read-only

`CAPABILITIES` is a read-only feature bitmap. Software must first validate the
`REVISION` register, then use `CAPABILITIES` to verify revision-specific
features.

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `VNPU_CAP_DOT_I8` | Signed INT8 dot-product command is implemented |
| 1 | `VNPU_CAP_VECTOR_LEN_8` | `VECTOR_LENGTH == 8` is accepted |
| 2 | `VNPU_CAP_VECTOR_LEN_16` | `VECTOR_LENGTH == 16` is accepted |
| 3 | `VNPU_CAP_JOB_ID` | `JOB_ID` is latched for submitted operations |
| 4 | `VNPU_CAP_FAULT_INJECTION` | `FAULT_CONTROL` fault injection is implemented |
| 5 | `VNPU_CAP_COMPLETION_IRQ` | `IRQ_COMPLETION` is implemented |
| 6 | `VNPU_CAP_ERROR_IRQ` | `IRQ_ERROR` is implemented |
| `31:7` | Reserved | Must read as `0` |

Expected values:

| Revision | Expected `CAPABILITIES` | Meaning |
|---|---:|---|
| A | `0x00000000` | Legacy Revision A behavior; features are implied by `REVISION == 1` |
| B | `0x0000007D` | Dot INT8, vector length 16, `JOB_ID`, fault injection, completion IRQ, and error IRQ |

Revision A keeps this register at `0` to preserve the current Revision A
contract. Revision B must set the documented feature bits and must keep the
expected value at `0x0000007D`. Revision B also accepts `VECTOR_LENGTH == 8` as
backward-compatible behavior; software must allow it based on `REVISION == 2`
even though the Revision B capability value does not set
`VNPU_CAP_VECTOR_LEN_8`.

## Control

Offset: `0x00C`  
Access: write-only, write-one action

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `CONTROL_START` | Start one dot-product operation |
| 1 | `CONTROL_RESET` | Reset device runtime state |

Writing `CONTROL_START` begins one operation if the device is idle and the request is valid. The MVP supports only one outstanding operation.

Writing `CONTROL_RESET` returns the device to idle state, clears pending IRQ status, clears the last error code, and clears the fault state.

## Status

Offset: `0x010`  
Access: read-only

| Value | Name | Meaning |
|---:|---|---|
| 0 | `STATUS_IDLE` | Device is ready for a new command |
| 1 | `STATUS_BUSY` | Operation is in progress |
| 2 | `STATUS_DONE` | Last operation completed successfully |
| 3 | `STATUS_ERROR` | Last operation failed |

## IRQ Status

Offset: `0x014`  
Access: read/write-one-to-clear

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `IRQ_COMPLETION` | Operation completed |
| 1 | `IRQ_ERROR` | Device error occurred |

To clear an interrupt bit, the driver writes `1` to the corresponding bit.

## IRQ Enable

Offset: `0x018`  
Access: read/write

1 is enable, 0 is disable.
Uses the same bit definitions as `IRQ_STATUS`.

An IRQ is raised only when the matching bit is set in both `IRQ_STATUS` and `IRQ_ENABLE`.

## Error Code

Offset: `0x01C`  
Access: read-only

| Value | Name | Meaning |
|---:|---|---|
| 0 | `VNPU_ERR_NONE` | No error |
| 1 | `VNPU_ERR_INVALID_LENGTH` | Unsupported vector length |
| 2 | `VNPU_ERR_BUSY` | Start requested while already busy |
| 3 | `VNPU_ERR_FORCED` | Fault injection forced an error |
| 4 | `VNPU_ERR_UNSUPPORTED_REVISION` | Unsupported revision |
| 5 | `VNPU_ERR_INTERNAL` | Internal device-model error |

## Job ID

Offset: `0x020`  
Access: read/write

Revision A:

```text
reads return 0
writes are ignored
```

Revision B:

```text
driver may write a job identifier before CONTROL_START
device preserves the job identifier for the submitted operation
```

## Vector Length

Offset: `0x024`  
Access: read/write

| Revision | Supported Values |
|---|---|
| A | `8` only | 
| B | `8` and `16` |

Unsupported values must produce `VNPU_ERR_INVALID_LENGTH`.
The reset value is revision-dependent: Revision A resets to `8`, and Revision B
resets to `16`.

## Input Registers

Offsets:

```text
INPUT_A[0]: 0x100
INPUT_A[1]: 0x104
INPUT_A[2]: 0x108
INPUT_A[3]: 0x10C

INPUT_B[0]: 0x120
INPUT_B[1]: 0x124
INPUT_B[2]: 0x128
INPUT_B[3]: 0x12C
```

Each 32-bit register packs four signed INT8 values.

Example packing:

```text
bits  7:0   element 0
bits 15:8   element 1
bits 23:16  element 2
bits 31:24  element 3
```

The device computes:

```text
result = sum(input_a[i] * input_b[i])
```

for `i = 0` to `VECTOR_LENGTH - 1`.

## Result

Offset: `0x140`  
Access: read-only

Contains the signed 32-bit dot-product result after successful completion.

## Fault Control

Offset: `0x180`  
Access: read/write

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `FAULT_IRQ_DROP` | Complete operation without raising an IRQ |
| 1 | `FAULT_STUCK_BUSY` | Leave device in busy state until reset |
| 2 | `FAULT_CORRUPT_RESULT` | Deliberately alter the computed result |
| 3 | `FAULT_FORCE_ERROR` | Finish with a device error |

Faults are test-only behavior and must be deterministic.
`FAULT_CONTROL` allows only one active fault bit. If software writes multiple bits, the device selects the lowest-numbered set bit and ignores the rest.

## Planned State Machine

```text
RESET
  -> IDLE
       -> START valid request
            -> BUSY
                 -> DONE
                    -> IDLE, if result is read.

                 -> ERROR until RESET
                 -> BUSY until RESET when FAULT_STUCK_BUSY is active
       -> START while BUSY
            -> remain BUSY, expose VNPU_ERR_BUSY
       -> RESET
            -> IDLE
```

## Driver Expectations

The Linux driver must:

- verify `DEVICE_ID`;
- reject unsupported `REVISION`;
- tolerate `CAPABILITIES == 0` for Revision A during probe;
- require the documented Revision B capability bits when `REVISION == 2`;
- validate `VECTOR_LENGTH` before starting an operation;
- enable IRQs before writing `CONTROL_START`;
- wait with a bounded device timeout from `125` ms through `1000` ms,
  inclusive;
- acknowledge `IRQ_STATUS` using RW1C semantics;
- read `RESULT` only after successful completion;
- read `ERROR_CODE` on failure;
- issue reset during timeout recovery.
