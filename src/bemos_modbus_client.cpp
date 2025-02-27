/*
 * modbus.cpp
 *
 *  Created on: 10.03.2017
 *	  Author: Jan Schöppach
 */

#include <getopt.h>
#include <modbus.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <ranges>
#include <string>
#include <unordered_set>
#include <vector>

#include "bemos_modbus_client/version.hpp"
#include "cxxopts.hpp"
#include "nlohmann/json.hpp"
#include "spdlog/async.h"
#include "spdlog/fmt/bin_to_hex.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

#ifdef ENABLE_SYSTEMD_STATUS
#include "spdlog/sinks/systemd_sink.h"
#endif

#include "bone_helper/customTypeTraits.hpp"
#include "bone_helper/jsonHelper.hpp"
#include "bone_helper/loopTimer.hpp"
#include "bone_helper/netHelper.hpp"
#include "bone_helper/system_helper.hpp"

using namespace bestsens;
using json = nlohmann::json;

namespace {
	constexpr auto login_user = "bemos-analysis";
	constexpr auto login_hash = "82e324d4dac1dacf019e498d6045835b"
								"3998def1c1cece4abf94a3743f149e20"
								"8f30276b3275fdbb8c60dea4a042c490"
								"d73168d41cf70f9cdc3e1e62eb43f8e4";

	struct mb_config {
		int nb_input_registers{0};
		int input_register_start{0};
		std::string mb_protocol{"tcp"};
		double mb_timeout{1.0};
		int mb_update_time{1000};
		std::string mb_tcp_target;
		int mb_tcp_port{502};
		int function_code{3};
		
		std::string mb_rtu_serialport{"/dev/ttyS1"};
		int mb_rtu_baud{9600};
		char mb_rtu_parity{'N'};
		int mb_rtu_databits{8};
		int mb_rtu_stopbits{1};

		int mb_slave{1};
	};

	auto initializeModbusContext(const mb_config& configuration) -> modbus_t* {
		modbus_t *ctx = nullptr;
		if (configuration.mb_protocol == "tcp") {
			spdlog::info("connecting to {}:{}", configuration.mb_tcp_target, configuration.mb_tcp_port);
			ctx = modbus_new_tcp(configuration.mb_tcp_target.c_str(), configuration.mb_tcp_port);
		} else {
			spdlog::info("using {} - {} {}{}{}", configuration.mb_rtu_serialport, configuration.mb_rtu_baud, configuration.mb_rtu_databits, configuration.mb_rtu_parity, configuration.mb_rtu_stopbits);
			ctx = modbus_new_rtu(configuration.mb_rtu_serialport.c_str(), configuration.mb_rtu_baud, configuration.mb_rtu_parity, configuration.mb_rtu_databits, configuration.mb_rtu_stopbits);
		}

		if (ctx == nullptr)
			throw std::runtime_error("failed to create modbus context");

		/*
		 * set modbus slave address
		 */
		if (modbus_set_slave(ctx, configuration.mb_slave) != 0) {
			modbus_free(ctx);
			throw std::runtime_error(fmt::format("could not set slave address to {}", configuration.mb_slave));
		}

		/*
		 * set modbus timeout
		 */
		struct timeval mb_timeout_t{};
		mb_timeout_t.tv_sec = static_cast<int>(configuration.mb_timeout);
		mb_timeout_t.tv_usec = static_cast<int>((configuration.mb_timeout-floor(configuration.mb_timeout)) * 1000000);

	#if (LIBMODBUS_VERSION_CHECK(3, 1, 2))
		if (modbus_set_response_timeout(ctx, static_cast<uint32_t>(mb_timeout_t.tv_sec),
										static_cast<uint32_t>(mb_timeout_t.tv_usec)) < 0)
			throw std::runtime_error("error setting modbus timeout");
	#else
		modbus_set_response_timeout(ctx, &mb_timeout_t);
	#endif

		if (modbus_connect(ctx) == -1) {
			modbus_free(ctx);
			throw std::runtime_error("failed to connect to modbus client, exiting");
		}

		return ctx;
	}

	auto loadConfigurationFile(const std::string& config_path) -> json {
		std::ifstream file;
		file.open(config_path);
		
		if (file.is_open()) {
			std::string str;
			std::string file_contents;

			while (std::getline(file, str)) {
				file_contents += str;
				file_contents.push_back('\n');
			}

			file.close();

			return json::parse(file_contents);
		} else {
			throw std::runtime_error("error opening configuration file");
		}
	}

	// NOLINTNEXTLINE(readability-function-cognitive-complexity)
	auto parseConfigurationFile(const json& mb_configuration, std::unique_ptr<bestsens::netHelper>& socket) -> mb_config {
		using namespace bestsens;
		mb_config configuration;

		configuration.mb_protocol = value_ig_type(mb_configuration, "protocol", "tcp");
		configuration.mb_timeout = value_ig_type(mb_configuration, "timeout", configuration.mb_timeout);
		configuration.function_code = value_ig_type(mb_configuration, "function", configuration.function_code);
		configuration.mb_update_time = value_ig_type(mb_configuration, "update_time", configuration.mb_update_time);
		configuration.mb_slave = value_ig_type(mb_configuration, "slave id", configuration.mb_slave);

		if (configuration.mb_protocol == "tcp") {
			configuration.mb_tcp_target = mb_configuration.at("server_address").get<std::string>();
			configuration.mb_tcp_port = value_ig_type(mb_configuration, "port", configuration.mb_tcp_port);
		} else if (configuration.mb_protocol == "rtu") {
			/*
			 * TODO: Modbus RTU still not tested!
			 */
			configuration.mb_rtu_serialport = mb_configuration.at("serial port").get<std::string>();
			configuration.mb_rtu_baud = mb_configuration.at("baudrate").get<int>();
			configuration.mb_rtu_parity = mb_configuration.at("parity").get<std::string>().front();
			configuration.mb_rtu_databits = mb_configuration.at("databits").get<int>();
			configuration.mb_rtu_stopbits = mb_configuration.at("stopbits").get<int>();
		} else {
			throw std::runtime_error("protocol type unknown");
		}

		std::unordered_set<std::string> sources_to_register;
		std::vector<int> input_registers;
		auto data_sources = json::array();

		if (mb_configuration.contains("map") && mb_configuration.at("map").is_array()) {
			for (const auto& e : mb_configuration.at("map")) {
				if (!e.is_null()) {
					const auto& source = e.at("source").get<std::string>();
					const auto& identifier = e.at("identifier").get<std::string>();
					const auto& input_register = e.at("address").get<int>();

					if (e.contains("name") && e.at("name").is_string()) {
						try {
							const auto& name = e.at("name").get<std::string>();
							const auto& unit = e.value("unit", "");
							const auto& decimals = e.value("decimals", 2);

							json element = {{"name", name},
											{"source", source},
											{"identifier", identifier},
											{"unit", unit},
											{"decimals", decimals}};

							data_sources.push_back(std::move(element));
						} catch (...) {}  // NOLINT(bugprone-empty-catch)
					}

					sources_to_register.emplace(source);
					input_registers.push_back(input_register);
				}
			}
		}

		if (socket != nullptr) {
			for (const auto &e : sources_to_register) {
				json k;
				auto this_data_sources = json::array();

				for (const auto& f : data_sources) {
					try {
						if (f.at("source").get<std::string>() == e) {
							this_data_sources.push_back(f);
						}
					} catch (...) {}  // NOLINT(bugprone-empty-catch)
				}

				socket->send_command("register_analysis", k, {{"name", e}, {"data_sources", this_data_sources}});
			}
		}

		if (!input_registers.empty()) {
			const auto min = *std::ranges::min_element(input_registers); 
			const auto max = *std::ranges::max_element(input_registers);

			configuration.nb_input_registers = max - min + 3;  // add three to allow last register to be 4 bytes wide
			configuration.input_register_start = min;
		} else {
			configuration.nb_input_registers = 0;
			configuration.input_register_start = 0;
		}

		return configuration;
	}

	// NOLINTBEGIN
	enum order_t { order_abcd, order_cdab, order_badc, order_dcba, order_invalid = -1 };
	NLOHMANN_JSON_SERIALIZE_ENUM(order_t, {
		{order_invalid, nullptr},
		{order_abcd, "abcd"},
		{order_cdab, "cdab"},
		{order_badc, "badc"},
		{order_dcba, "dcba"},
	})

	enum register_type_t { type_i16, type_u16, type_i32, type_u32, type_i64, type_u64, type_f32, type_invalid = -1 };
	NLOHMANN_JSON_SERIALIZE_ENUM(register_type_t, {
		{type_invalid, nullptr},
		{type_i16, "i16"},
		{type_u16, "u16"},
		{type_i32, "i32"},
		{type_u32, "u32"},
		{type_i64, "i64"},
		{type_u64, "u64"},
		{type_f32, "f32"},
	})
	// NOLINTEND

	auto getValueU16(const uint16_t* start, uint16_t offset) -> uint16_t {
		if (start == nullptr) {
			throw std::invalid_argument("out of bounds");
		}

		return start[offset];
	}

	auto getValueI16(const uint16_t* start, uint16_t offset) -> int16_t {
		uint16_t ival = getValueU16(start, offset);

		int16_t val = 0;
		std::memcpy(&val, &ival, sizeof(val));
		
		return val;
	}

	auto getValueU32(const uint16_t* start, uint16_t offset) -> uint32_t {
		uint32_t val = getValueU16(start, offset);
		return (val << 16u) + getValueU16(start, offset + 1);
	}


	auto getValueI32(const uint16_t* start, uint16_t offset) -> int32_t {
		uint32_t ival = getValueU32(start, offset);
		
		int32_t val = 0;
		std::memcpy(&val, &ival, sizeof(val));

		return val;
	}

	auto getValueU64(const uint16_t* start, uint16_t offset) -> uint64_t {
		const uint64_t val = getValueU32(start, offset);
		return (val << 32u) + getValueU32(start, offset + 2);
	}


	auto getValueI64(const uint16_t* start, uint16_t offset) -> int64_t {
		uint64_t ival = getValueU64(start, offset);
		
		int64_t val = 0;
		std::memcpy(&val, &ival, sizeof(val));

		return val;
	}

	auto getValueF32(const uint16_t* start, uint16_t offset, const order_t order) -> float {
		if (start == nullptr) {
			throw std::invalid_argument("out of bounds");
		}

		switch (order) {
		default:
			throw std::invalid_argument("unknown byte order");
		case order_abcd:
			return modbus_get_float_abcd(start + offset);
		case order_cdab:
			return modbus_get_float_cdab(start + offset);
		case order_badc:
			return modbus_get_float_badc(start + offset);
		case order_dcba:
			return modbus_get_float_dcba(start + offset);
		}
	}

	template<typename NumericType = uint16_t>
	auto interpolate(double from, double to, double value, NumericType int_from, NumericType int_to) -> NumericType {
		return static_cast<NumericType>(
			static_cast<double>(int_from) *
			((1 - (value - from) / (to - from)) + static_cast<double>(int_to) * ((value - from) / (to - from))));
	}

	auto readRegisters(modbus_t* ctx, std::vector<uint16_t>& reg, const mb_config& configuration) -> int {
		int retval = 0;

		if (configuration.nb_input_registers == 0) {
			throw std::runtime_error("no input registers to read");
			return 0;
		}

		if (configuration.function_code == 4) {
			retval = modbus_read_input_registers(ctx, configuration.input_register_start,
												 configuration.nb_input_registers, reg.data());
		} else {
			retval = modbus_read_registers(ctx, configuration.input_register_start,
										   configuration.nb_input_registers, reg.data());
		}

		if (retval == -1) {
			throw std::runtime_error(fmt::format("error reading registers, exiting: {}", modbus_strerror(errno)));
		}

		return retval;
	}

	auto getAttributeData(const json& map, const std::vector<uint16_t>& reg, const mb_config& configuration) -> json {
		json attribute_data;

		for (const auto& e : map) {
			try {
				const auto source = e.at("source").get<std::string>();
				const auto identifier = e.at("identifier").get<std::string>();
				const auto register_type = e.at("type").get<register_type_t>();
				const auto address =
					coerceCast<uint16_t>(e.at("address").get<int>() - configuration.input_register_start);

				const auto order = [&e]() -> order_t {
					try {
						return e.at("order").get<order_t>();
					} catch (...) {}  // NOLINT(bugprone-empty-catch)

					return order_abcd;
				}();

				double value{};

				switch (register_type) {
					case type_f32:	value = static_cast<double>(getValueF32(reg.data(), address, order)); break;
					case type_i16:	value = static_cast<double>(getValueI16(reg.data(), address)); break;
					case type_u16:	value = static_cast<double>(getValueU16(reg.data(), address)); break;
					case type_i32:	value = static_cast<double>(getValueI32(reg.data(), address)); break;
					case type_u32:	value = static_cast<double>(getValueU32(reg.data(), address)); break;
					case type_i64:	value = static_cast<double>(getValueI64(reg.data(), address)); break;
					case type_u64:	value = static_cast<double>(getValueU64(reg.data(), address)); break;
					default: throw std::invalid_argument("register type not available"); break;
				}

				try {
					if (e.at("scale").is_number()) {
						value *= e.at("scale").get<double>();
					} else if (e.at("scale").is_array()) {
						auto scale = e.at("scale").get<std::array<int, 4>>();
						value = interpolate(scale.at(0), scale.at(1), value, scale.at(2), scale.at(3));
					}
				} catch (...) {}  // NOLINT(bugprone-empty-catch)

				attribute_data[source][identifier] = value;
			} catch (const std::exception& err) {
				spdlog::error("error getting value: {}", err.what());
			}
		}

		return attribute_data;
	}

	auto initializeSpdlog(const std::string& application_name) {
		spdlog::init_thread_pool(8192, 1);

		auto console = spdlog::stdout_color_mt<spdlog::async_factory>("console");
		console->set_pattern("%v");

		#ifdef ENABLE_SYSTEMD_STATUS
		auto create_systemd_logger = [](std::string name) {
			std::vector<spdlog::sink_ptr> sinks;
			sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_st>());
			sinks.push_back(std::make_shared<spdlog::sinks::systemd_sink_st>());

			sinks[1]->set_pattern("%v");

			auto logger = std::make_shared<spdlog::async_logger>(name, begin(sinks), end(sinks), spdlog::thread_pool(), spdlog::async_overflow_policy::overrun_oldest);
			spdlog::register_logger(logger);
			return logger;
		};

		auto systemd_logger = create_systemd_logger(application_name);
		systemd_logger->flush_on(spdlog::level::err); 
		spdlog::set_default_logger(systemd_logger);
		auto default_logger = systemd_logger;
		#else
		auto stdout_logger = spdlog::stdout_color_mt<spdlog::async_factory>(application_name);
		stdout_logger->flush_on(spdlog::level::err); 
		spdlog::set_default_logger(stdout_logger);
		auto default_logger = stdout_logger;
		#endif

		spdlog::flush_every(std::chrono::seconds(5));

		return default_logger;
	}
}

auto main(int argc, char **argv) -> int {
	bool daemon = false;
	bool skip_bemos = false;

	auto default_logger = initializeSpdlog("bemos_modbus_client");

	std::string conn_target = "localhost";
	std::string conn_port = "6450";

	std::string config_path;

	std::string username = std::string(login_user);
	std::string password = std::string(login_hash);

	/*
	 * parse commandline options
	 */
	{
		cxxopts::Options options("bemos-modbus-client", "BeMoS one Modbus client application");

		options.add_options()
			("version", "print version string")
			("h,help", "print help")
			("d,daemonize", "daemonize server", cxxopts::value<bool>(daemon))
			("v,verbose", "verbose output")
			("c,connect", "connect to given host", cxxopts::value<std::string>(conn_target)->default_value(conn_target))
			("p,port", "connect to given port", cxxopts::value<std::string>(conn_port)->default_value(conn_port))
			("username", "username used to connect", cxxopts::value<std::string>(username)->default_value(std::string(login_user)))
			("password", "plain text password used to connect", cxxopts::value<std::string>())
			("suppress_syslog", "do not output syslog messages to stdout")
			("config", "path to configuration file", cxxopts::value<std::string>(), "FILE")
			("skip_bemos", "do not use bemos", cxxopts::value<bool>(skip_bemos))
		;

		try {
			options.parse_positional({"config"});

			auto result = options.parse(argc, argv);

			if (result.count("help") != 0u) {
				spdlog::get("console")->info(options.help());
				return EXIT_SUCCESS;
			}

			if (result.count("version") != 0u) {
				spdlog::get("console")->info("bemos-modbus-client version: {}", appVersion());

				if (result.count("verbose") != 0u) {
					spdlog::get("console")->info("git branch: {}", appGitBranch());
					spdlog::get("console")->info("git revision: {}", appGitRevision());
					spdlog::get("console")->info("compiled @ {}", appCompileDate());
					spdlog::get("console")->info("compiler version: {}", appCompilerVersion());
				}

				return EXIT_SUCCESS;
			}

			if (daemon) {
				#ifdef ENABLE_SYSTEMD_STATUS
				if(default_logger->sinks().size() > 1)
					default_logger->sinks().erase(default_logger->sinks().begin());
				#endif

				spdlog::info("daemonized");
			}

			if (result.count("suppress_syslog") != 0U) {
				#ifdef ENABLE_SYSTEMD_STATUS
				if(default_logger->sinks().size() > 1)
					default_logger->sinks().erase(default_logger->sinks().begin());
				#endif
			}

			if (result.count("config") != 0u) {
				config_path = result["config"].as<std::string>();
				spdlog::info("using configuration file: {}", config_path);
			}

			if (result.count("verbose") != 0u) {
				spdlog::set_level(spdlog::level::debug);
				spdlog::info("verbose output enabled");
			}

			if (result.count("verbose") > 1) {
				spdlog::set_level(spdlog::level::trace);
				spdlog::info("trace output enabled");
			}

			if (result.count("password") != 0u) {
				password = bestsens::netHelper::sha512(result["password"].as<std::string>());
			}

			if (skip_bemos) {
				spdlog::info("don't use bemos");
			}
		} catch (const std::exception& e) {
			spdlog::get("console")->error(e.what());
			return EXIT_FAILURE;
		}
	}

	spdlog::info("starting bemos-modbus-client {}", appVersion());

	if (config_path.empty()) {
		spdlog::error("configuration path not set");
		return EXIT_FAILURE;
	}

	/*
	 * Test IEEE 754
	 */
	if (!std::numeric_limits<float>::is_iec559)
		spdlog::warn("application wasn't compiled with IEEE 754 standard, floating point values may be out of standard");

	/*
	 * open socket
	 */
	std::unique_ptr<bestsens::netHelper> socket{};

	if (!skip_bemos) {
		socket = std::make_unique<bestsens::netHelper>(conn_target, conn_port);

		/*
		 * connect to socket
		 */
		if (socket->connect() != 0) {
			spdlog::critical("connection to BeMoS failed");
			return EXIT_FAILURE;
		}

		/*
		 * login
		 */
		if (socket->login(username, password) == 0) {
			spdlog::critical("login to bemos failed");
			return EXIT_FAILURE;
		}
	}

	/*
	 * read configuration file
	 */
	spdlog::debug("opening configuration file...");
	const auto mb_configuration = loadConfigurationFile(config_path);
	
	spdlog::debug("parsing configuration file...");
	const auto configuration = parseConfigurationFile(mb_configuration, socket);
	spdlog::debug("finished parsing configuration file");

	std::vector<uint16_t> reg(configuration.nb_input_registers);
	auto *ctx = initializeModbusContext(configuration);

	/* Deamonize */
	if (daemon) {
		bestsens::system_helper::daemonize();
		spdlog::info("daemon created");
	} else {
		spdlog::debug("skipped daemonizing");
	}

	bestsens::loopTimer timer(std::chrono::milliseconds(configuration.mb_update_time), false);

	bestsens::system_helper::systemd::ready();

	while (true) {
		bestsens::system_helper::systemd::watchdog();
		timer.wait_on_tick();

		readRegisters(ctx, reg, configuration);

		const auto& map = mb_configuration.at("map");
		const auto attribute_data = getAttributeData(map, reg, configuration);

		if (socket != nullptr) {
			for (const auto &e : attribute_data.items()) {
				if (!e.value().is_null()) {
					json k;
					const json payload = {
						{"name", e.key()},
						{"data", e.value()}
					};

					if (socket->send_command("new_data", k, payload) == 0) {
						spdlog::error("error updating algorithm_config");
					}
				}
			}
		}

		spdlog::debug("{}", attribute_data.dump(2));
	}

	modbus_close(ctx);
	modbus_free(ctx);

	spdlog::debug("exited");

	return EXIT_SUCCESS;
}
