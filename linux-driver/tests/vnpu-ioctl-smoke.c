#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "vnpu_uapi.h"

#define FOR_REV \
	for(int i=0 ; i<10; i++)


int main(void)
{
    int fd;
    struct vnpu_info info;
    struct vnpu_dot_request req;
    struct vnpu_status stats;
	
	printf("open /dev/vnpu0\n");
    fd = open("/dev/vnpu0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
FOR_REV {

	printf("start VNPU_IOCTL_GET_INFO\n");
    if (ioctl(fd, VNPU_IOCTL_GET_INFO, &info) < 0) {
        perror("GET_INFO EEROR: ");
        return 1;
    }

    printf("abi=%u device_id=0x%x\n",
           info.abi_version, info.device_id);

    /* req = (struct vnpu_dot_request) { */
    /*     .abi_version = 1, */
    /*     .vector_length = 8, */
    /*     .timeout_ms = 255, */
    /*     .input_a = { 0x04030201, 0x08070605, 0, 0 }, */
    /*     .input_b = { 0x01010101, 0x01010101, 0, 0 }, */
    /* }; */

    req = (struct vnpu_dot_request) {
        .abi_version = 1,
        .vector_length = 16,
        .timeout_ms = 255,
        .input_a = { 0x04030201, 0x08070605, 0x04030201, 0x08070605 },
        .input_b = { 0x01010101, 0x01010101, 0x01010101, 0x01010101 },
    };


	printf("start VNPU_IOCTL_RUN_DOT\n");
    if (ioctl(fd, VNPU_IOCTL_RUN_DOT, &req) < 0) {
        perror("RUN_DOT ERROR: ");
        return 1;
    }

    printf("result=%d driver_status=%d device_error=%u job_id=%d\n",
           req.result, req.driver_status, req.device_error, req.job_id);


	printf("start VNPU_IOCTL_GET_STAT\n");
    if (ioctl(fd, VNPU_IOCTL_GET_STAT, &stats) < 0) {
        perror("GET_STAT ERROR: ");
        return 1;
    }

    printf("submitted=%llu completed=%llu timed_out=%llu errors=%llu resets=%llu\n",
           (unsigned long long)stats.submitted,
           (unsigned long long)stats.completed,
           (unsigned long long)stats.timed_out,
           (unsigned long long)stats.device_error,
           (unsigned long long)stats.resets);
}
    close(fd);
    return 0;
}
