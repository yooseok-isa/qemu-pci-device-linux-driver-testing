#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/ioctl.h>
#include <unistd.h>

#include <vnpu/vnpu-hal.hpp>

#include "vnpu_uapi.h"

LinuxVnpuDevice::LinuxVnpuDevice(std::string path)
{
	fd_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
	if (fd_ < 0) {
		throw std::system_error(errno, std::generic_category(), "open /dev/vnpu0");
	}
}

LinuxVnpuDevice::~LinuxVnpuDevice()
{
	if (fd_ >= 0) {
		::close(fd_);
	}
}

DeviceInfo LinuxVnpuDevice::vnpu_get_info() const
{
	struct vnpu_info info = {};

	if (::ioctl(fd_, VNPU_IOCTL_GET_INFO, &info) < 0) {
		throw VnpuError(VnpuErrorType::system_error,
				"unspecified error : get info",
				errno);
	}

	return DeviceInfo {
		.abi_version = info.abi_version,
		.device_id = info.device_id,
		.revision = info.revision,
		.capabilities = info.capabilities
	};
}

DotProductResult LinuxVnpuDevice::vnpu_run_dot(
	std::span<const std::int8_t> input_a,
	std::span<const std::int8_t> input_b,
	std::chrono::milliseconds timeout)
{
	struct vnpu_dot_request dot_request = {};
	
	if (input_a.size() != input_b.size()) {
		throw VnpuError(VnpuErrorType::validation_error,
				"input_a and input_b length mismatch");
	}
	if (input_a.size() != 8 && input_a.size() != 16) {
		throw VnpuError(VnpuErrorType::validation_error,
				"Revision A requires vector length 8 or 16");
	}
	if (input_b.size() != 8 && input_b.size() != 16) {
		throw VnpuError(VnpuErrorType::validation_error,
				"Revision A requires vector length 8 or 16");
	}
	if (timeout.count() < 125 || timeout.count() > 1000) {
		throw VnpuError(VnpuErrorType::validation_error,
				"timeout must be positive and must be under 1000");
	}
	
	pack_int8_input(input_a, dot_request.input_a);
	pack_int8_input(input_b, dot_request.input_b);
	dot_request.abi_version = 1;
	dot_request.vector_length = static_cast<__u32>(input_a.size());
	dot_request.timeout_ms = static_cast<__u32>(timeout.count());
	
	if (::ioctl(fd_, VNPU_IOCTL_RUN_DOT, &dot_request) < 0) {
		if(errno == ETIMEDOUT) {
			throw VnpuError(VnpuErrorType::device_error,
					"Device time out",
					errno);
		}
		
		if(errno == EFAULT){
			throw VnpuError(VnpuErrorType::system_error,
					"failed to copy struct",
					errno);
		}
		
		if(errno == EINVAL){
			if(dot_request.driver_status == DRIVER_STATUS_INV_REV){
				throw VnpuError(VnpuErrorType::validation_error,
						"invalid revision version",
						errno);
			}
			if(dot_request.driver_status == DRIVER_STATUS_INV_LEN){
				throw VnpuError(VnpuErrorType::validation_error,
						"invalid vector length",
						errno);
			}
			if(dot_request.driver_status == DRIVER_STATUS_INV_TIME){
				throw VnpuError(VnpuErrorType::validation_error,
						"invalid timeout count",
						errno);
			}
		}
		if(errno == EIO){
			throw VnpuError(VnpuErrorType::system_error,
					"Device force error",
					errno);
		}
	}
	
	return DotProductResult {
		.result = dot_request.result,
		.device_error = static_cast<std::int32_t>(dot_request.device_error),
		.status = static_cast<DotProductResult::driver_status>(dot_request.driver_status),
	};
}

void LinuxVnpuDevice::vnpu_reset()
{
	if (::ioctl(fd_, VNPU_IOCTL_RESET) < 0) {
		throw VnpuError(VnpuErrorType::system_error,
				"unspecified error : reset",
				errno);
	}
}

void LinuxVnpuDevice::vnpu_set_fault(FaultType type)
{
	struct vnpu_fault_request fault = {};
	
	fault.abi_version = 1;
	fault.fault_mask = static_cast<std::uint32_t>(type);
	
	if (::ioctl(fd_, VNPU_IOCTL_SET_FAULT, &fault) < 0) {
		if(errno == EFAULT){
				throw VnpuError(VnpuErrorType::system_error,
						"failed to copy struct",
						errno);
		}
		
		if(errno == EINVAL){
				throw VnpuError(VnpuErrorType::validation_error,
						"invalid fault mask: multi bit or invalid bit",
						errno);
		}
	}
}

DeviceStats LinuxVnpuDevice::vnpu_get_stats() const
{
	struct vnpu_status status = {};

	if (::ioctl(fd_, VNPU_IOCTL_GET_STAT, &status) < 0) {
		if(errno == EFAULT){
			throw VnpuError(VnpuErrorType::system_error,
					"failed to copy struct",
					errno);
		}
		else {
			throw VnpuError(VnpuErrorType::system_error,
					"unspecified error",
					errno);
		}
	}

	return DeviceStats {
		.submitted = status.submitted,
		.completed = status.completed,
		.timed_out = status.timed_out,
		.device_error = status.device_error,
		.resets = status.resets,
	};
}

void LinuxVnpuDevice::pack_int8_input(
	std::span<const std::int8_t> input,
	std::uint32_t (&packed)[4])
{
	for (std::uint32_t& word : packed) {
		word = 0;
	}

	const std::size_t word_count = (input.size() + 3) / 4;
	for (std::size_t out = 0; out < word_count; ++out) {
		const std::size_t in = out * 4;
		std::uint32_t word = 0;

		for (std::size_t byte = 0; byte < 4 && in + byte < input.size(); ++byte) {
			word |= static_cast<std::uint32_t>(
				static_cast<std::uint8_t>(input[in + byte])) << (byte * 8);
		}

		packed[out] = word;
	}
}

MockVnpuDevice::MockVnpuDevice(std::string path)
{
	if(std::strcmp(path.c_str(), "/dev/vnpu0")){
		throw std::runtime_error("open /dev/vnpu0");
	}
}

DeviceInfo MockVnpuDevice::vnpu_get_info() const{
	
	return DeviceInfo {
		.abi_version = 1,
		.device_id = 0x564E5055,
		.revision = 2,
		.capabilities = 0x7D,
	};
}

DotProductResult MockVnpuDevice::vnpu_run_dot(
		std::span<const std::int8_t> input_a,
		std::span<const std::int8_t> input_b, 
		std::chrono::milliseconds timeout) {

	if (input_a.size() != input_b.size()) {
		throw VnpuError(VnpuErrorType::validation_error,
				"input_a and input_b length mismatch");
	}
	if (input_a.size() != 8 && input_a.size() != 16) {
		throw VnpuError(VnpuErrorType::validation_error,
				"Revision A requires vector length 8 or 16");
	}
	if (input_b.size() != 8 && input_b.size() != 16) {
		throw VnpuError(VnpuErrorType::validation_error,
				"Revision A requires vector length 8 or 16");
	}
	if (timeout.count() < 125 || timeout.count() > 1000) {
		throw VnpuError(VnpuErrorType::validation_error,
				"timeout must be positive and must be under 1000");
	}

	// submitted++;
	// completed++;
	
	if(fault_mask_ == static_cast<std::uint32_t>(FaultType::corrupt_result)){
		stats_.submitted++;
		stats_.completed++;
		return DotProductResult {
			.result = dot_product(input_a, input_b) ^ 0b01,
			.device_error = 0,
			.status = DotProductResult::DRIVER_OK,
		};
	}

	if(fault_mask_ == static_cast<std::uint32_t>(FaultType::stuck_busy) || 
			fault_mask_ == static_cast<std::uint32_t>(FaultType::irq_drop)){
		stats_.submitted++;
		stats_.timed_out++;
		throw VnpuError(VnpuErrorType::device_error, "timeout");
	}

	if(fault_mask_ == static_cast<std::uint32_t>(FaultType::force_error)){
		stats_.submitted++;
		stats_.device_error++;
		throw VnpuError(VnpuErrorType::device_error, "force error");
	}

	stats_.submitted++;
	stats_.completed++;
	return DotProductResult {
		.result = dot_product(input_a, input_b),
		.device_error = 0,
		.status = DotProductResult::DRIVER_OK,
	};
}

void MockVnpuDevice::vnpu_reset() { 
	stats_.resets++; 
	fault_mask_ = static_cast<std::uint32_t>(FaultType::none);
}

void MockVnpuDevice::vnpu_set_fault(FaultType type){
		fault_mask_ = static_cast<std::uint32_t>(type);
}

DeviceStats MockVnpuDevice::vnpu_get_stats() const {
	
	return DeviceStats{
		.submitted = MockVnpuDevice::stats_.submitted,
		.completed = MockVnpuDevice::stats_.completed,
		.timed_out = MockVnpuDevice::stats_.timed_out,
		.device_error = MockVnpuDevice::stats_.device_error,
		.resets = MockVnpuDevice::stats_.resets,
	};
}


int32_t MockVnpuDevice::dot_product(
		std::span<const std::int8_t> input_a,
		std::span<const std::int8_t> input_b) {
	
	std::int32_t result = 0;
	for(size_t i=0; i < input_a.size(); i++){
		result += input_a[i] * input_b[i];
	}
	return result;
}

// void MockVnpuDevice::pack_int8_input(
// 	std::span<const std::int8_t> input,
// 	std::uint32_t (&packed)[4])
// {
// 	for (std::uint32_t& word : packed) {
// 		word = 0;
// 	}
//
// 	const std::size_t word_count = (input.size() + 3) / 4;
// 	for (std::size_t out = 0; out < word_count; ++out) {
// 		const std::size_t in = out * 4;
// 		std::uint32_t word = 0;
//
// 		for (std::size_t byte = 0; byte < 4 && in + byte < input.size(); ++byte) {
// 			word |= static_cast<std::uint32_t>(
// 				static_cast<std::uint8_t>(input[in + byte])) << (byte * 8);
// 		}
//
// 		packed[out] = word;
// 	}
// }
