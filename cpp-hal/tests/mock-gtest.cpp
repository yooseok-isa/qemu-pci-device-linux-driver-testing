#include "gtest/gtest.h"
#include <array>
#include <chrono>
#include <cstdint>

#include <gtest/gtest.h>

#include <vnpu/vnpu-hal.hpp>

using namespace std::chrono_literals;

namespace { // code anonymous

class MockVnpuDeviceTest : public ::testing::Test{
	protected:
		MockVnpuDevice dev;
		std::array<const std::int8_t, 8> input_a = {1,2,3,4,5,6,7,8};
		std::array<const std::int8_t, 8> input_b = {8,7,6,5,4,3,2,1};
		std::chrono::milliseconds time_out{255};
		std::chrono::milliseconds over_time{1200};
		
		uint32_t gtest_run_dot(
				std::array<const std::int8_t, 8> a, 
				std::array<const std::int8_t, 8> b)
		{
			uint32_t result = 0;
			for(size_t i=0; i < a.size(); i++){
				result += a[i]*b[i];
			}
			return result;
		}
};

TEST_F(MockVnpuDeviceTest, ReturnDeviceInfo){
	const DeviceInfo info = dev.vnpu_get_info();

	EXPECT_EQ(info.abi_version, 1u);
	EXPECT_EQ(info.device_id, 0x564E5055u);
	EXPECT_EQ(info.revision, 0u);
	EXPECT_EQ(info.capabilities, 0u);
}
TEST_F(MockVnpuDeviceTest, RunDotProduct){
	const DotProductResult result = dev.vnpu_run_dot(input_a, input_b, time_out); 
	EXPECT_EQ(result.result, gtest_run_dot(input_a, input_b)); 
	EXPECT_EQ(result.device_error, 0u);

	EXPECT_EQ(result.status, DotProductResult::DRIVER_OK);
}

TEST_F(MockVnpuDeviceTest, RunDotProductOverTime){
	
	EXPECT_THROW(dev.vnpu_run_dot(input_a, input_b, over_time), VnpuError);
}

TEST_F(MockVnpuDeviceTest, DeviceResets){
	DeviceStats after_stats;
	
	dev.vnpu_reset();
	after_stats = dev.vnpu_get_stats();

	EXPECT_EQ(after_stats.resets, 1u);
}

TEST_F(MockVnpuDeviceTest, CheckResetFaultMask){
	
	DotProductResult result;
	
	dev.vnpu_set_fault(FaultType::corrupt_result);
	result = dev.vnpu_run_dot(input_a, input_b, time_out);
	
	EXPECT_EQ(result.result, gtest_run_dot(input_a, input_b) ^ 0b1); 

	dev.vnpu_reset(); 
	result = dev.vnpu_run_dot(input_a, input_b, time_out); 
	
	EXPECT_EQ(result.result, gtest_run_dot(input_a, input_b));
	
}

TEST_F(MockVnpuDeviceTest, CheckClearFaultMask){
	
	DotProductResult result;
	
	dev.vnpu_set_fault(FaultType::corrupt_result);
	result = dev.vnpu_run_dot(input_a, input_b, time_out);
	
	EXPECT_EQ(result.result, gtest_run_dot(input_a, input_b) ^ 0b1); 

	dev.vnpu_set_fault(FaultType::none); 
	result = dev.vnpu_run_dot(input_a, input_b, time_out); 
	
	EXPECT_EQ(result.result, gtest_run_dot(input_a, input_b));
	
}

TEST_F(MockVnpuDeviceTest, SetFaultmaskIRQ){
	DeviceStats a_stats;
	
	dev.vnpu_set_fault(FaultType::irq_drop);
	EXPECT_THROW(dev.vnpu_run_dot(input_a, input_b, time_out), VnpuError);
	a_stats = dev.vnpu_get_stats();
	
	EXPECT_EQ(a_stats.timed_out, 1);
	EXPECT_EQ(a_stats.completed, 0u);
	EXPECT_EQ(a_stats.device_error, 0u);
	EXPECT_EQ(a_stats.resets, 0u);
}

TEST_F(MockVnpuDeviceTest, IRQDropCheck){
	dev.vnpu_set_fault(FaultType::irq_drop);

	try {
		dev.vnpu_run_dot(input_a, input_b, time_out);
		FAIL() << "Expected VnpuError";
	} catch(const VnpuError& error){
		EXPECT_EQ(error.type(), VnpuErrorType::device_error);
		EXPECT_STREQ(error.what(), "timeout");
		EXPECT_EQ(error.errnum(), 0);
		EXPECT_EQ(error.device_error(), 0);
	}
}

TEST_F(MockVnpuDeviceTest, SetFaultmaskBUSY){
	DeviceStats a_stats;

	dev.vnpu_set_fault(FaultType::stuck_busy);
	EXPECT_THROW(dev.vnpu_run_dot(input_a, input_b, time_out), VnpuError);
	a_stats = dev.vnpu_get_stats();

	EXPECT_EQ(a_stats.submitted, 1);
	EXPECT_EQ(a_stats.timed_out, 1);
	EXPECT_EQ(a_stats.completed, 0u);
	EXPECT_EQ(a_stats.device_error, 0u);
	EXPECT_EQ(a_stats.resets, 0u);

}

TEST_F(MockVnpuDeviceTest, SetFaultmaskFORCE){
	DeviceStats a_stats;

	dev.vnpu_set_fault(FaultType::force_error);
	EXPECT_THROW(dev.vnpu_run_dot(input_a, input_b, time_out), VnpuError);
	a_stats = dev.vnpu_get_stats();

	EXPECT_EQ(a_stats.submitted, 1);
	EXPECT_EQ(a_stats.device_error, 1);
	EXPECT_EQ(a_stats.completed, 0u);
	EXPECT_EQ(a_stats.timed_out, 0u);
	EXPECT_EQ(a_stats.resets, 0u);
	
}

TEST_F(MockVnpuDeviceTest, ForeceErrorCheck){
	dev.vnpu_set_fault(FaultType::force_error);

	try{
		dev.vnpu_run_dot(input_a, input_b, time_out);
		FAIL() << "Expected VnpuError";
	} catch(const VnpuError& error){
		EXPECT_EQ(error.type(), VnpuErrorType::device_error);
		EXPECT_STREQ(error.what(), "force error");
		EXPECT_EQ(error.errnum(), 0);
		EXPECT_EQ(error.device_error(), 0);
	}
}

TEST_F(MockVnpuDeviceTest, SetFaultmaskCORRUPT){
	DeviceStats a_stats;
	DotProductResult result;
	
	dev.vnpu_set_fault(FaultType::corrupt_result);
	result = dev.vnpu_run_dot(input_a, input_b, time_out);
	a_stats = dev.vnpu_get_stats();
	
	EXPECT_NE(result.result, gtest_run_dot(input_a, input_b));
	EXPECT_EQ(a_stats.submitted, 1);
	EXPECT_EQ(a_stats.completed, 1);
	EXPECT_EQ(a_stats.device_error, 0u);
	EXPECT_EQ(a_stats.timed_out, 0u);
	EXPECT_EQ(a_stats.resets, 0u);
	
}

}; //namespace end.



