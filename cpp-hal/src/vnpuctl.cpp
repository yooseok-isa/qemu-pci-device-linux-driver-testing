#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <vnpu/vnpu-hal.hpp>

struct DotInput {
	std::vector<std::int8_t> input_a;
	std::vector<std::int8_t> input_b;
	std::chrono::milliseconds timeout;
};

static void print_usage()
{
	std::cerr
		<< "Usage:\n"
		<< "  vnpuctl info --json\n"
		<< "  vnpuctl run-dot --input input.json --json\n"
		<< "  vnpuctl inject-fault irq-drop|stuck-busy|corrupt-result|force-error\n"
		<< "  vnpuctl clear-faults\n"
		<< "  vnpuctl reset\n"
		<< "  vnpuctl stats --json\n";
}

static std::string get_option(int argc, char** argv, std::string_view option)
{
	for (int i = 1; i + 1 < argc; ++i) {
		if (std::string_view(argv[i]) == option) {
			return argv[i + 1];
		}
	}

	throw std::runtime_error(std::string("missing option: ") + std::string(option));
}

static std::string get_option_or(int argc, char** argv, std::string_view option, std::string fallback)
{
	for (int i = 1; i + 1 < argc; ++i) {
		if (std::string_view(argv[i]) == option) {
			return argv[i + 1];
		}
	}

	return fallback;
}

static bool has_flag(int argc, char** argv, std::string_view flag)
{
	for (int i = 1; i < argc; i++) {
		if (std::string_view(argv[i]) == flag) {
			return true;
		}
	}
	return false;
}

static int find_command_index(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i) {
		const std::string_view arg(argv[i]);
		if (arg == "--device") {
			++i;
			continue;
		}
		if (!arg.starts_with("--")) {
			return i;
		}
	}

	return -1;
}

static std::string_view trim(std::string_view text)
{
	while (!text.empty() && (text.front() == ' ' || text.front() == '\n' || text.front() == '\t')) {
		text.remove_prefix(1);
	}
	while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\t')) {
		text.remove_suffix(1);
	}

	return text;
}

static std::vector<std::int8_t> parse_int8_list(std::string_view text)
{
	std::vector<std::int8_t> out;
	while (!text.empty()) {
		const std::size_t comma = text.find(',');
		const std::string_view token = trim(text.substr(0, comma));
		int value = 0;
		const char* begin = token.data();
		const char* end = token.data() + token.size();
		const auto result = std::from_chars(begin, end, value);

		if (result.ec != std::errc() || result.ptr != end) {
			throw std::runtime_error("invalid int8 list");
		}
		if (value < -128 || value > 127) {
			throw std::runtime_error("int8 list value out of range");
		}

		out.push_back(static_cast<std::int8_t>(value));
		if (comma == std::string_view::npos) {
			break;
		}
		text.remove_prefix(comma + 1);
	}

	if (out.size() != 8 && out.size() != 16) {
		throw std::runtime_error("Revision B requires 8 or 16 elements");
	}
	return out;
}

static std::vector<std::int8_t> parse_json_int8_array(const std::string& body, std::string_view field)
{
	const std::string key = "\"" + std::string(field) + "\"";
	const std::size_t key_pos = body.find(key);
	if (key_pos == std::string::npos) {
		throw std::runtime_error("missing field: " + std::string(field));
	}

	const std::size_t begin = body.find('[', key_pos);
	const std::size_t end = body.find(']', begin);
	if (begin == std::string::npos || end == std::string::npos || end <= begin) {
		throw std::runtime_error("invalid array field: " + std::string(field));
	}

	return parse_int8_list(std::string_view(body).substr(begin + 1, end - begin - 1));
}

static int parse_json_int(const std::string& body, std::string_view field)
{
	const std::string key = "\"" + std::string(field) + "\"";
	const std::size_t key_pos = body.find(key);
	if (key_pos == std::string::npos) {
		throw std::runtime_error("missing field: " + std::string(field));
	}

	const std::size_t colon = body.find(':', key_pos);
	if (colon == std::string::npos) {
		throw std::runtime_error("invalid integer field: " + std::string(field));
	}

	const std::size_t begin = body.find_first_not_of(" \n\t", colon + 1);
	std::size_t end = body.find_first_of(",}\n\t ", begin);
	if (begin == std::string::npos) {
		throw std::runtime_error("invalid integer field: " + std::string(field));
	}
	if (end == std::string::npos) {
		end = body.size();
	}

	int value = 0;
	const char* first = body.data() + begin;
	const char* last = body.data() + end;
	const auto result = std::from_chars(first, last, value);
	if (result.ec != std::errc() || result.ptr != last) {
		throw std::runtime_error("invalid integer field: " + std::string(field));
	}

	return value;
}

static DotInput parse_run_dot_input_json(const std::string& path)
{
	std::ifstream file(path);
	if (!file) {
		throw std::runtime_error("failed to open: " + path);
	}

	const std::string body(
		(std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());

	DotInput input;
	input.input_a = parse_json_int8_array(body, "input_a");
	input.input_b = parse_json_int8_array(body, "input_b");

	if (input.input_a.size() != input.input_b.size()) {
		throw std::runtime_error("input_a and input_b length mismatch");
	}

	const int timeout_ms = parse_json_int(body, "timeout_ms");
	if (timeout_ms < 125 || timeout_ms > 1000) {
		throw std::runtime_error("timeout_ms must be 125~1000.");
	}

	input.timeout = std::chrono::milliseconds(timeout_ms);
	return input;
}

static const char* driver_status_to_string(DotProductResult::driver_status status)
{
	switch (status) {
	case DotProductResult::DRIVER_OK:
		return "ok";
	case DotProductResult::DRIVER_TIMEOUT:
		return "timeout";
	case DotProductResult::DRIVER_DEVICE_ERROR:
		return "device_error";
	case DotProductResult::DRIVER_INV_REV:
		return "invalid_reivsion";
	case DotProductResult::DRIVER_INV_LEN:
		return "invalid_vector_length";
	case DotProductResult::DRIVER_INV_TIME:
		return "invalid timeout count";
	case DotProductResult::DRIVER_INV_MULTIBIT:
		return "not allow multi bit in fualt mask";
	case DotProductResult::DRIVER_INV_BIT:
		return "invalid fault mask";
	}

	return "unknown";
}

static void print_error_schema(
	std::string_view command,
	std::string_view error_type,
	std::string_view message,
	int errnum,
	std::uint32_t device_error)
{
	std::cout
		<< "{\n"
		<< "\t\"command\": \"" << command << "\",\n"
		<< "\t\"status\": \"" << "error" << "\",\n"
		<< "\t\"error_type\" : \"" << error_type << "\",\n"
		<< "\t\"message\" : \"" << message << "\",\n"
		<< "\t\"errno\" :" << errnum << ",\n"
		<< "\t\"device_error\" :" << device_error << "\n"
		<< "}\n";
}

static void print_vnpu_error_schema(std::string_view command, const VnpuError& error)
{
	print_error_schema(command, to_string(error.type()), error.what(), error.errnum(), error.device_error());
}

static void print_internal_error_schema(std::string_view command, const std::exception& error)
{
	print_error_schema(command, "internal_error", error.what(), 0, 0);
}

static FaultType parse_fault_type(std::string_view value)
{
	if (value == "irq-drop") {
		return FaultType::irq_drop;
	}
	if (value == "stuck-busy") {
		return FaultType::stuck_busy;
	}
	if (value == "corrupt-result") {
		return FaultType::corrupt_result;
	}
	if (value == "force-error") {
		return FaultType::force_error;
	}

	throw std::runtime_error("unknown fault type");
}

int main(int argc, char** argv)
{
	try {
		const int command_index = find_command_index(argc, argv);
		if (command_index < 0) {
			print_usage();
			return 1;
		}

		const std::string_view command = argv[command_index];
		LinuxVnpuDevice dev(get_option_or(argc, argv, "--device", "/dev/vnpu0"));

		if (command == "info") {
			try {
				const DeviceInfo info = dev.vnpu_get_info();
				std::cout
					<< "{\n"
					<< "\t\"command\": \"" << argv[command_index] << "\",\n"
					<< "\t\"status\": \"" << "success" << "\",\n"
					<< "\t\"abi_version\":" << info.abi_version << ",\n"
					<< "\t\"device_id\":" << info.device_id << ",\n"
					<< "\t\"revision\":" << info.revision << ",\n"
					<< "\t\"capabilities\":" << info.capabilities << "\n"
					<< "}\n";
			}
			catch(const VnpuError& e) {
				print_vnpu_error_schema(command, e);
			}
			catch(const std::exception& e){
				print_internal_error_schema(command, e);
			}
			return 0;
		}

		if (command == "run-dot") {
			DotInput input;
			input = parse_run_dot_input_json(get_option(argc, argv, "--input"));

			try {
				const DotProductResult dot = dev.vnpu_run_dot(input.input_a, input.input_b, input.timeout);
				std::cout
					<< "{\n"
					<< "\t\"command\": \"" << argv[command_index] << "\",\n"
					<< "\t\"status\": \"" << "success" << "\",\n"
					<< "\t\"result\":" << dot.result << ",\n"
					<< "\t\"driver_status\": \"" << driver_status_to_string(dot.status) << "\",\n"
					<< "\t\"device_error\":" << dot.device_error << "\n"
					<< "}\n";
			}
			catch(const VnpuError& e) {
				print_vnpu_error_schema(command, e);
			}
			catch(const std::exception& e){
				print_internal_error_schema(command, e);
			}
			return 0;
		}

		if (command == "inject-fault") {
			if (command_index + 1 >= argc) {
				throw std::runtime_error("missing fault type");
			}
			try {
				dev.vnpu_set_fault(parse_fault_type(argv[command_index + 1]));
				std::cout
					<< "{\n"
					<< "\t\"command\": \"" << argv[command_index] << "\",\n"
					<< "\t\"status\": \"" << "success" << "\",\n"
					<< "\t\"set_fault\": \"" << argv[command_index+1] << "\"\n"
					<< "}\n";
			}
			catch(const VnpuError& e) {
				print_vnpu_error_schema(command, e);
			}
			catch(const std::exception& e){
				print_internal_error_schema(command, e);
			}
			return 0;
		}

		if (command == "clear-faults") {
			try {
				dev.vnpu_set_fault(FaultType::none);
					std::cout
						<< "{\n"
						<< "\t\"command\": \"" << argv[command_index] << "\",\n"
						<< "\t\"status\": \"" << "success" << "\"\n"
						<< "}\n";
				}
			catch(const VnpuError& e) {
				print_vnpu_error_schema(command, e);
			}
			catch(const std::exception& e){
				print_internal_error_schema(command, e);
			}
			return 0;
		}
		
		if (command == "reset") {
			try{
				dev.vnpu_reset();
				std::cout
					<< "{\n"
					<< "\t\"command\": \"" << argv[command_index] << "\",\n"
					<< "\t\"status\": \"" << "success" << "\"\n"
					<< "}\n";
			}
			catch(const VnpuError& e) {
				print_vnpu_error_schema(command, e);
			}
			catch(const std::exception& e){
				print_internal_error_schema(command, e);
			}
			return 0;
		}

		if (command == "stats") {
			try {
				const DeviceStats stats = dev.vnpu_get_stats();
				std::cout
					<< "{\n"
					<< "\t\"command\": \"" << argv[command_index] << "\",\n"
					<< "\t\"status\": \"" << "success" << "\",\n"
					<< "\t\"submitted\":" << stats.submitted << ",\n"
					<< "\t\"completed\":" << stats.completed << ",\n"
					<< "\t\"timed_out\":" << stats.timed_out << ",\n"
					<< "\t\"device_error\":" << stats.device_error << ",\n"
					<< "\t\"resets\":" << stats.resets << "\n"
					<< "}\n";
			}
			catch(const VnpuError& e) {
				print_vnpu_error_schema(command, e);
			}
			catch(const std::exception& e){
				print_internal_error_schema(command, e);
			}
			return 0;
		}
	} catch (const std::exception& error) {
		if (has_flag(argc, argv, "--json")) {
			print_internal_error_schema("argument parse", error);
		} else {
			std::cerr << "error: " << error.what() << "\n";
		}

		return 1;
	}
}
