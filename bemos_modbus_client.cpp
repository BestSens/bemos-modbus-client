/*
 * modbus.cpp
 *
 *  Created on: 10.03.2017
 *	  Author: Jan Schöppach
 */

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <cstring>
#include <string>
#include <exception>
#include <modbus.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unordered_set>
#include <fstream>

#include "version.hpp"
#include "cxxopts.hpp"
#include "nlohmann/json.hpp"

#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/fmt/bin_to_hex.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/daily_file_sink.h"

#ifdef ENABLE_SYSTEMD_STATUS
#include "spdlog/sinks/systemd_sink.h"
#endif

#include "libs/bone_helper/loopTimer.hpp"
#include "libs/bone_helper/netHelper.hpp"
#include "libs/bone_helper/system_helper.hpp"

using namespace bestsens;
using json = nlohmann::json;

#define LOGIN_USER "bemos-analysis"
#define LOGIN_HASH "82e324d4dac1dacf019e498d6045835b3998def1c1cece4abf94a3743f149e208f30276b3275fdbb8c60dea4a042c490d73168d41cf70f9cdc3e1e62eb43f8e4"

#define USERID 1200
#define GROUPID 880

namespace {
	json mb_configuration;

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

	uint16_t getValue_u16(const uint16_t* start, uint16_t offset) {
		if(start + offset == nullptr)
			throw std::invalid_argument("out of bounds");

		return start[offset];
	}

	int16_t getValue_i16(const uint16_t* start, uint16_t offset) {
		uint16_t ival = getValue_u16(start, offset);

		int16_t val;
		std::memcpy(&val, &ival, sizeof(val));
		
		return val;
	}

	uint32_t getValue_u32(const uint16_t* start, uint16_t offset) {
		uint32_t val = getValue_u16(start, offset);
		return (val << 16) + getValue_u16(start, offset + 1);
	}


	int32_t getValue_i32(const uint16_t* start, uint16_t offset) {
		uint32_t ival = getValue_u32(start, offset);
		
		int32_t val;
		std::memcpy(&val, &ival, sizeof(val));

		return val;
	}

	uint64_t getValue_u64(const uint16_t* start, uint16_t offset) {
		uint64_t val = getValue_u32(start, offset);
		return (val << 32) + getValue_u32(start, offset + 2);
	}


	int64_t getValue_i64(const uint16_t* start, uint16_t offset) {
		uint64_t ival = getValue_u64(start, offset);
		
		int64_t val;
		std::memcpy(&val, &ival, sizeof(val));

		return val;
	}

	float getValue_f32(const uint16_t* start, uint16_t offset, const order_t order) {
		if(start + offset == nullptr)
			throw std::invalid_argument("out of bounds");

		float output;

		switch(order) {
			default: throw std::invalid_argument("unknown byte order"); break;
			case order_abcd: output = modbus_get_float_abcd(start + offset); break;
			case order_cdab: output = modbus_get_float_cdab(start + offset); break;
			case order_badc: output = modbus_get_float_badc(start + offset); break;
			case order_dcba: output = modbus_get_float_dcba(start + offset); break;
		}

		return output;
	}

	template<typename _NumericType = uint16_t>
	_NumericType interpolate(double from, double to, double value, _NumericType int_from, _NumericType int_to) {
		return int_from * (1 - (value - from) / (to - from)) + int_to * ((value - from) / (to - from));
	}

	#ifdef ENABLE_SYSTEMD_STATUS
	auto create_systemd_logger(std::string name = "") {
		std::vector<spdlog::sink_ptr> sinks;
		sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_st>());
		sinks.push_back(std::make_shared<spdlog::sinks::systemd_sink_st>());

		sinks[1]->set_pattern("%v");

		auto logger = std::make_shared<spdlog::async_logger>(name, begin(sinks), end(sinks), spdlog::thread_pool(), spdlog::async_overflow_policy::overrun_oldest);
		spdlog::register_logger(logger);
		return logger;
	}
	#endif
}

int main(int argc, char **argv){
	bool daemon = false;
	bool skip_bemos = false;

	auto console = spdlog::stdout_color_mt<spdlog::async_factory>("console");
	console->set_pattern("%v");

	#ifdef ENABLE_SYSTEMD_STATUS
	auto systemd_logger = create_systemd_logger("bemos_mqtt");
	systemd_logger->flush_on(spdlog::level::err); 
	spdlog::set_default_logger(systemd_logger);
	#else
	auto stdout_logger = spdlog::stdout_color_mt<spdlog::async_factory>("bemos_mqtt");
	stdout_logger->flush_on(spdlog::level::err); 
	spdlog::set_default_logger(stdout_logger);
	#endif

	spdlog::flush_every(std::chrono::seconds(5));

	std::string conn_target = "localhost";
	std::string conn_port = "6450";

	std::string config_path;

	std::string username = std::string(LOGIN_USER);
	std::string password = std::string(LOGIN_HASH);

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
			("username", "username used to connect", cxxopts::value<std::string>(username)->default_value(std::string(LOGIN_USER)))
			("password", "plain text password used to connect", cxxopts::value<std::string>())
			("suppress_syslog", "do not output syslog messages to stdout")
			("config", "path to configuration file", cxxopts::value<std::string>(), "FILE")
			("skip_bemos", "do not use bemos", cxxopts::value<bool>(skip_bemos))
		;

		try {
			options.parse_positional({"config"});

			auto result = options.parse(argc, argv);

			if(result.count("help")) {
				spdlog::get("console")->info(options.help());
				return EXIT_SUCCESS;
			}

			if(result.count("version")) {
				spdlog::get("console")->info("bemos-modbus-client version: {}", app_version());

				if(result.count("verbose")) {
					spdlog::get("console")->info("git branch: {}", app_git_branch());
					spdlog::get("console")->info("git revision: {}", app_git_revision());
					spdlog::get("console")->info("compiled @ {}", app_compile_date());
					spdlog::get("console")->info("compiler version: {}", app_compiler_version());
					spdlog::get("console")->info("compiler flags: {}", app_compile_flags());
					spdlog::get("console")->info("linker flags: {}", app_linker_flags());
				}

				return EXIT_SUCCESS;
			}

			if(daemon) {
				#ifdef ENABLE_SYSTEMD_STATUS
				if(systemd_logger->sinks().size() > 1)
					systemd_logger->sinks().erase(systemd_logger->sinks().begin());
				#endif

				spdlog::info("start daemonized");
			}

			if(result.count("suppress_syslog")) {
				#ifdef ENABLE_SYSTEMD_STATUS
				if(systemd_logger->sinks().size() > 1)
					systemd_logger->sinks().erase(systemd_logger->sinks().begin());
				#endif
			}

			if(result.count("config")) {
				config_path = result["config"].as<std::string>();
				spdlog::info("using configuration file: {}", config_path);
			}

			if(result.count("verbose")) {
				spdlog::set_level(spdlog::level::debug);
				spdlog::info("verbose output enabled");
			}

			if(result.count("verbose") > 1) {
				spdlog::set_level(spdlog::level::trace);
				spdlog::info("trace output enabled");
			}

			if(result.count("password")) {
				password = bestsens::netHelper::sha512(result["password"].as<std::string>());
			}

			if(skip_bemos) {
				spdlog::info("don't use bemos");
			}
		} catch(const std::exception& e) {
			spdlog::get("console")->error(e.what());
			return EXIT_FAILURE;
		}
	}

	spdlog::info("starting bemos-modbus-client {}", app_version());

	/*
	 * Test IEEE 754
	 */
	if(!std::numeric_limits<float>::is_iec559)
		spdlog::warn("application wasn't compiled with IEEE 754 standard, floating point values may be out of standard");

	/*
	 * open socket
	 */
	bestsens::jsonNetHelper* socket;

	if(!skip_bemos) {	
		socket = new bestsens::jsonNetHelper(conn_target, conn_port);

		/*
		 * connect to socket
		 */
		if(socket->connect()) {
			spdlog::critical("connection to BeMoS failed");
			return EXIT_FAILURE;
		}

		/*
		 * login
		 */
		if(!socket->login(username, password)) {
			spdlog::critical("login to bemos failed");
			return EXIT_FAILURE;
		}
	}

	/*
	 * read configuration file
	 */
	spdlog::debug("opening configuration file...");
	{
		std::ifstream file;
		file.open(config_path);
		
		if(file.is_open()) {
			std::string str;
			std::string file_contents;

			while(std::getline(file, str)) {
				file_contents += str;
				file_contents.push_back('\n');
			}

			file.close();

			try {
				mb_configuration = json::parse(file_contents);
			} catch(const json::exception& e) {
				spdlog::critical("error loading configuration file: {}", e.what());
				return EXIT_FAILURE;
			}
		} else {
			spdlog::critical("error opening configuration file");
			return EXIT_FAILURE;
		}
	}

	/*
	 * check all defined sources and add register them
	 * also determine required address range
	 */
	int nb_input_registers = 0;
	int input_register_start = 0;
	std::string mb_protocol = "tcp";
	double mb_timeout = 1.0;
	std::string mb_tcp_target = "";
	int mb_tcp_port = 502;
	
	std::string mb_rtu_serialport = "/dev/ttyS1";
	int mb_rtu_baud = 9600;
	std::string mb_rtu_parity = "N";
	int mb_rtu_databits = 8;
	int mb_rtu_stopbits = 1;
	int mb_rtu_slave = 1;

	spdlog::debug("parsing configuration file...");

	try {
		mb_protocol = mb_configuration.at("protocol").get<std::string>();
	} catch(...) {
		mb_protocol = "tcp";
	}

	try {
		mb_timeout = mb_configuration.at("timeout").get<double>();
	} catch(...) {}

	if(mb_protocol.compare("tcp") == 0) {
		mb_tcp_target = mb_configuration.at("server_address").get<std::string>();

		try {
			mb_tcp_port = mb_configuration.at("port").get<int>();
		} catch(...) {}
	} else if(mb_protocol.compare("rtu") == 0) {
		/*
		 * TODO: Modbus RTU still not tested!
		 */
		mb_rtu_serialport = mb_configuration.at("serial port").get<std::string>();
		mb_rtu_baud = mb_configuration.at("baudrate").get<int>();
		mb_rtu_parity = mb_configuration.at("parity").get<std::string>();
		mb_rtu_databits = mb_configuration.at("databits").get<int>();
		mb_rtu_stopbits = mb_configuration.at("stopbits").get<int>();
		mb_rtu_slave = mb_configuration.at("slave id").get<int>();
	} else {
		spdlog::critical("protocol type unknown");
		return EXIT_FAILURE;
	}


	{
		std::unordered_set<std::string> sources_to_register;
		std::vector<int> input_registers;

		for(const auto &e : mb_configuration.at("map").items()) {
			if(!e.value().is_null()) {
				std::string source = e.value().at("source").get<std::string>();
				int input_register = e.value().at("address").get<int>();

				sources_to_register.emplace(source);
				input_registers.push_back(input_register);
			}
		}

		if(!skip_bemos) {
			for(const auto &e : sources_to_register) {
				json k;
				socket->send_command("register_analysis", k, {{"name", e}});
			}
		}

		int max = *max_element(std::begin(input_registers), std::end(input_registers));
		int min = *min_element(std::begin(input_registers), std::end(input_registers));

		nb_input_registers = max - min + 3; // add three to allow last register to be 4 bytes wide
		input_register_start = min;

		if(nb_input_registers > 256) {
			spdlog::critical("maximum input register range of 256 reached");
			return EXIT_FAILURE;
		}
	}
	spdlog::debug("finished parsing configuration file");

	modbus_t *ctx;
	if(mb_protocol.compare("tcp") == 0) {
		spdlog::info("connecting to {}:{}", mb_tcp_target, mb_tcp_port);
		ctx = modbus_new_tcp(mb_tcp_target.c_str(), mb_tcp_port);
	} else {
		spdlog::info("using {} - {} {}{}{}", mb_rtu_serialport, mb_rtu_baud, mb_rtu_databits, mb_rtu_parity.front(), mb_rtu_stopbits);
		ctx = modbus_new_rtu(mb_rtu_serialport.c_str(), mb_rtu_baud, mb_rtu_parity.front(), mb_rtu_databits, mb_rtu_stopbits);
	}

	if(!ctx) {
		spdlog::critical("failed to create modbus context, exiting");
		return EXIT_FAILURE;
	}

	/*
	 * set modbus slave address
	 */
	if(0 != modbus_set_slave(ctx, mb_rtu_slave)) {
		spdlog::critical("could not set slave address to {}", mb_rtu_slave);
		return EXIT_FAILURE;
	}

	/*
	 * set modbus timeout
	 */
	struct timeval mb_timeout_t;
	mb_timeout_t.tv_sec = static_cast<int>(mb_timeout);
	mb_timeout_t.tv_usec = static_cast<int>((mb_timeout-floor(mb_timeout)) * 1000000);
#if (LIBMODBUS_VERSION_CHECK(3, 1, 2))
	if(modbus_set_response_timeout(ctx, mb_timeout_t.tv_sec, mb_timeout_t.tv_usec) < 0) {
		spdlog::critical("error setting modbus timeout");
		return EXIT_FAILURE;
	}
#else
	modbus_set_response_timeout(ctx, &mb_timeout_t);
#endif

	if(modbus_connect(ctx) == -1) {
		spdlog::critical("failed to connect to modbus client, exiting");
		modbus_free(ctx);
		return EXIT_FAILURE;
	}

	/* Deamonize */
	if(daemon) {
		bestsens::system_helper::daemonize();
		spdlog::info("daemon created");
	} else {
		spdlog::debug("skipped daemonizing");
	}

	bestsens::loopTimer timer(std::chrono::seconds(1), 0);

	bestsens::system_helper::systemd::ready();

	while(1) {
		bestsens::system_helper::systemd::watchdog();
		timer.wait_on_tick();

		uint16_t reg[256];

		int num = modbus_read_input_registers(ctx, input_register_start, nb_input_registers, reg);
		
		if(num == -1) {
			spdlog::critical("error reading registers, exiting: {}", modbus_strerror(errno));
			modbus_close(ctx);
			modbus_free(ctx);
			return EXIT_FAILURE;
		}

		json attribute_data;

		for(auto e : mb_configuration.at("map")) {
			try {
				std::string source = e.at("source").get<std::string>();
				std::string identifier = e.at("identifier").get<std::string>();
				register_type_t register_type = e.at("type").get<register_type_t>();
				unsigned int address = e.at("address").get<unsigned int>() - input_register_start;

				order_t order = order_invalid;
				try {
					order = e.at("order").get<order_t>();
				} catch(...) {
					order = order_abcd;
				}

				switch(register_type) {
					case type_f32:	attribute_data[source][identifier] = getValue_f32(reg, address, order); break;
					case type_i16:	attribute_data[source][identifier] = getValue_i16(reg, address); break;
					case type_u16:	attribute_data[source][identifier] = getValue_u16(reg, address); break;
					case type_i32:	attribute_data[source][identifier] = getValue_i32(reg, address); break;
					case type_u32:	attribute_data[source][identifier] = getValue_u32(reg, address); break;
					case type_i64:	attribute_data[source][identifier] = getValue_i64(reg, address); break;
					case type_u64:	attribute_data[source][identifier] = getValue_u64(reg, address); break;
					default: throw std::invalid_argument("register type not available"); break;
				}
			} catch(const std::exception& err) {
				spdlog::error("error getting value: {}", err.what());
			}
		}

		if(!skip_bemos) {
			for(const auto &e : attribute_data.items()) {
				if(!e.value().is_null()) {
					json k;
					const json payload = {
						{"name", e.key()},
						{"data", e.value()}
					};

					if(!socket->send_command("new_data", k, payload))
						spdlog::error("error updating algorithm_config");
				}
			}
		}

		spdlog::debug("{}", attribute_data.dump(2));
	}

	modbus_close(ctx);
	modbus_free(ctx);

	if(!skip_bemos)
		delete(socket);

	spdlog::debug("exited");

	return EXIT_SUCCESS;
}
