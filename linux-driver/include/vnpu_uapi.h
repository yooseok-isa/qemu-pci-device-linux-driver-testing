#ifndef _VNPU_H
#define _VNPU_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define VNPU_IOCTL_MAGIC 'V'

#define VNPU_IOCTL_GET_INFO _IOR(VNPU_IOCTL_MAGIC, 0x00, struct vnpu_info)
#define VNPU_IOCTL_RUN_DOT _IOWR(VNPU_IOCTL_MAGIC, 0x01, struct vnpu_dot_request)
#define VNPU_IOCTL_RESET _IO(VNPU_IOCTL_MAGIC, 0x02)
#define VNPU_IOCTL_SET_FAULT _IOW(VNPU_IOCTL_MAGIC, 0x03, struct vnpu_fault_request)
#define VNPU_IOCTL_GET_STAT _IOR(VNPU_IOCTL_MAGIC, 0x04, struct vnpu_status)

//driver_status
#define DRIVER_STATUS_OK 0
#define DRIVER_STATUS_TIMEOUT 1
#define DRIVER_STATUS_DEVICE_ERROR 2
#define DRIVER_STATUS_INV_REV 3
#define DRIVER_STATUS_INV_LEN 4
#define DRIVER_STATUS_INV_TIME 5
#define DRIVER_STATUS_INV_MULTIMASK 6
#define DRIVER_STATUS_INV_BIT 7


struct vnpu_info {
	__u32 abi_version;
	__u32 device_id;
	__u32 revision;  
	__u32 capabilities;  
};

struct vnpu_dot_request {
	__u32 abi_version;
	__u32 job_id;
	__u32 vector_length;
	__u32 timeout_ms;
	__u32 input_a[4];
	__u32 input_b[4];
	__s32 result;
	__s32 driver_status;
	__u32 device_error;
};

struct vnpu_fault_request {
	__u32 abi_version;
	__u32 fault_mask;
};

struct vnpu_status {
	__u32 abi_version;
	__u64 submitted;
	__u64 completed;
	__u64 timed_out;
	__u64 device_error;
	__u64 resets;
};

#endif /* _VNPU_H */
