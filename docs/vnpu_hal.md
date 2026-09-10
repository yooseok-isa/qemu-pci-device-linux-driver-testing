# VNPU HAL and CLI

Status: implemented HAL/CLI contract under Revision B validation.

This document defines the C++ hardware abstraction layer (HAL) contract for
VNPU userspace software. The HAL sits above the Linux driver UAPI and hides raw
`ioctl`, file descriptor, and revision-specific details from applications and
validation tests.

## Scope

Milestone 4 provides a clean userspace interface on top of `/dev/vnpu0`.

Required deliverables:

- `IVnpuDevice` public interface;
- `LinuxVnpuDevice` backend using the Linux driver UAPI;
- `MockVnpuDevice` backend for deterministic unit tests without QEMU;
- structured error model;
- `vnpuctl` command-line tool with stable JSON output;
- GoogleTest coverage for HAL behavior;
- public API documentation.

The HAL must not expose MMIO register offsets or raw BAR access. Register-level
behavior remains documented in `docs/register_map.md`; driver userspace ABI
behavior remains documented in `docs/uapi.md` and `linux-driver/include/vnpu_uapi.h`.

## Architecture Position

```text
pytest validation
      |
      v
vnpuctl JSON CLI
      |
      v
C++ HAL: IVnpuDevice
      |
      +--> LinuxVnpuDevice -> /dev/vnpu0 ioctl -> Linux driver -> QEMU VNPU
      |
      +--> MockVnpuDevice  -> deterministic in-process backend
```

Tests above the HAL must use HAL or CLI concepts only. They must not hard-code
MMIO offsets, interrupt bits, or driver-private implementation details.

## Public Interface

The recommended public abstraction is:

```cpp
class IVnpuDevice {
public:
    virtual DeviceInfo get_info() const = 0;

    virtual DotProductResult run_dot_product(
        std::span<const std::int8_t> lhs,
        std::span<const std::int8_t> rhs,
        std::chrono::milliseconds timeout) = 0;

    virtual void reset() = 0;
    virtual void inject_fault(FaultType type) = 0;
    virtual DeviceStats get_stats() const = 0;

    virtual ~IVnpuDevice() = default;
};
```

The canonical C++ HAL header is `cpp-hal/include/vnpu/vnpu-hal.hpp`.
It should continue evolving toward this interface before the HAL is treated as
complete.

## Data Model

### `DeviceInfo`

`DeviceInfo` represents stable device identity and ABI information.

Required fields:

| Field | Type | Meaning |
|---|---|---|
| `abi_version` | `std::uint32_t` | Linux UAPI ABI version, currently `1` |
| `device_id` | `std::uint32_t` | VNPU MMIO device signature, expected `0x564E5055` |
| `revision` | `std::uint32_t` | Hardware revision reported by the driver |
| `capabilities` | `std::uint32_t` | Feature bitmap reported by the driver; Revision A may report `0` |

The current UAPI header exposes `abi_version`, `device_id`, `revision`, and
`capabilities` through `struct vnpu_info`. The HAL must return those fields
without inventing feature support from missing or unsupported hardware bits.

### `DotProductResult`

`DotProductResult` represents one completed or failed dot-product request.

Required fields:

| Field | Type | Meaning |
|---|---|---|
| `result` | `std::int32_t` | Signed INT32 dot-product result when status is OK |
| `driver_status` | enum | Driver-level status such as OK, timeout, or device error |
| `device_error` | `std::uint32_t` | Device error code reported by the driver |

The Linux backend maps this to `struct vnpu_dot_request`.

### `DeviceStats`

`DeviceStats` is diagnostic, not a performance ABI.

Required fields:

| Field | Type | Meaning |
|---|---|---|
| `submitted` | `std::uint64_t` | Accepted run submissions |
| `completed` | `std::uint64_t` | Successful completions |
| `timed_out` | `std::uint64_t` | Timeout paths |
| `device_error` | `std::uint64_t` | Device-reported failures |
| `resets` | `std::uint64_t` | Reset operations |

The Linux backend maps this to `struct vnpu_status` and
`VNPU_IOCTL_GET_STAT`.

### `FaultType`

Required fault values:

| HAL value | Driver fault bit | Meaning |
|---|---:|---|
| `irq_drop` | bit 0 | Complete without raising an interrupt |
| `stuck_busy` | bit 1 | Keep the device busy until reset |
| `corrupt_result` | bit 2 | Deterministically alter the result |
| `force_error` | bit 3 | Complete with a device error |
| `none` | `0` | Clear fault injection |

Fault injection is test-only behavior. It must not be represented as a
production hardware feature.

## Linux Backend

`LinuxVnpuDevice` communicates with the kernel driver through `/dev/vnpu0`.

Required behavior:

- open the device node in the constructor or an explicit factory;
- close the file descriptor through RAII;
- use `VNPU_IOCTL_GET_INFO` for `get_info()`;
- use `VNPU_IOCTL_RUN_DOT` for `run_dot_product()`;
- use `VNPU_IOCTL_RESET` for `reset()`;
- use `VNPU_IOCTL_SET_FAULT` for `inject_fault()`;
- use `VNPU_IOCTL_GET_STAT` for `get_stats()`;
- translate `errno`, driver status, and device error into distinct C++ errors;
- avoid hidden retry unless a retry rule is explicitly documented.

The backend must validate request shape before issuing `ioctl`.

Revision-aware vector rules:

- `lhs.size() == rhs.size()`;
- Revision A supports only length `8`;
- Revision B supports length `8` and length `16`;
- inputs are signed INT8 values;
- length `8` inputs are packed into `struct vnpu_dot_request::input_a[0..1]`
  and `input_b[0..1]` in little-endian byte order;
- length `16` inputs use all four packed input words;
- unused upper input words must be written as zero for length `8`;
- `timeout` must be within the inclusive device timeout range: `125` ms through
  `1000` ms.

Milestone 4 must keep the same public API for Revision A and B. Revision B
support must accept both length `8` and length `16`; requests for unsupported
lengths must fail with a structured unsupported-operation error.

## Mock Backend

`MockVnpuDevice` must support HAL unit tests without QEMU, a loaded kernel
module, or `/dev/vnpu0`.

Required behavior:

- return deterministic `DeviceInfo`;
- compute signed INT8 dot products in process;
- enforce the same public validation rules as `LinuxVnpuDevice`;
- simulate fault injection deterministically;
- update `DeviceStats` counters consistently;
- expose Revision A and Revision B behavior through constructor options or test
  fixtures.

The mock backend is not a replacement for integration testing. It exists to
make HAL behavior and error mapping testable in normal host unit tests.

## Error Model

The HAL must distinguish at least these categories:

| Category | Examples |
|---|---|
| validation error | mismatched vector sizes, unsupported length, invalid timeout |
| system error | `open`, `close`, or `ioctl` failing with `errno` |
| driver timeout | `VNPU_IOCTL_RUN_DOT` returns timeout status or `-ETIMEDOUT` |
| device error | device reports a fault/error code |
| unsupported operation | Revision B-only behavior requested on Revision A |

Applications and tests must be able to tell whether a failure came from caller
validation, the Linux syscall layer, driver timeout handling, or device fault
reporting.

## CLI

Executable name:

```text
vnpuctl
```

Required commands:

```bash
vnpuctl info --json
vnpuctl run-dot --input <json-file> --json
vnpuctl inject-fault irq-drop
vnpuctl inject-fault stuck-busy
vnpuctl inject-fault corrupt-result
vnpuctl inject-fault force-error
vnpuctl clear-faults
vnpuctl reset
vnpuctl stats --json
```

JSON output is the stable interface for pytest. Human-readable output may be
added, but automated validation must use JSON.

`run-dot` input must be read from a JSON file. This keeps validation vectors
reusable across manual tests, pytest, and CI.

Required `run-dot` input JSON shape:

```json
{
  "input_a": [1, 2, 3, 4, 5, 6, 7, 8],
  "input_b": [8, 7, 6, 5, 4, 3, 2, 1],
  "timeout_ms": 125
}
```

`input_a` and `input_b` contain signed INT8 values. Revision A input files must
provide exactly 8 elements per input vector. Revision B input files may provide
8 or 16 elements using the same field names. The CLI must reject mismatched
vector lengths, values outside the signed INT8 range, missing fields, and
timeouts outside `125` through `1000` ms before issuing the HAL request.

Suggested JSON shapes:

```json
{
  "abi_version": 1,
  "device_id": 1447977045,
  "revision": 1,
  "capabilities": 0
}
```

```json
{
  "result": 36,
  "driver_status": "ok",
  "device_error": 0
}
```

```json
{
  "submitted": 1,
  "completed": 1,
  "timed_out": 0,
  "device_error": 0,
  "resets": 0
}
```

JSON field names must remain stable once pytest integration depends on them.

## Unit Test Requirements

GoogleTest coverage must include:

- reserved capability handling;
- Revision A fixed length `8`;
- Revision B length `8` and `16` behavior through the mock backend;
- unsupported length rejection;
- signed INT8 packing and dot-product correctness;
- timeout/fault error mapping;
- stats counter updates;
- RAII behavior for backend lifetime where practical.

The acceptance target is that HAL unit tests run without QEMU.

## Acceptance Criteria

Milestone 4 is complete when:

- application code uses HAL APIs and contains no register offsets;
- `LinuxVnpuDevice` and `MockVnpuDevice` exist;
- the HAL hides raw UAPI structure packing from callers;
- `vnpuctl` supports info, run, reset, fault injection, fault clearing, and
  stats;
- JSON output is stable and documented;
- validation errors, Linux syscall failures, driver timeouts, and device errors
  are distinguishable;
- HAL unit tests pass without QEMU;
- API documentation is present and matches the implemented public interface.

## Current Gaps

As of this document, `cpp-hal` only contains an initial stub:

- `include/vnpu/vnpu-hal.hpp` now defines `IVnpuDevice`;
- `src/vnpu-hal.cpp` has empty function bodies;
- no Linux backend, mock backend, CLI, or tests are implemented yet.

These gaps should be closed before marking Milestone 4 complete in
`PROJECT_STATUS.md`.
