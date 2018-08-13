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

#include "version.hpp"
#include "rio_exception.hpp"
#include "libs/cxxopts/include/cxxopts.hpp"
#include "libs/bone_helper/loopTimer.hpp"
#include "libs/json/single_include/nlohmann/json.hpp"
#include "libs/bone_helper/netHelper.hpp"
#include "libs/bone_helper/system_helper.hpp"

using namespace bestsens;
using json = nlohmann::json;

system_helper::LogManager logfile("bemos-modbus-client");

#define LOGIN_USER "bemos-analysis"
#define LOGIN_HASH "82e324d4dac1dacf019e498d6045835b3998def1c1cece4abf94a3743f149e208f30276b3275fdbb8c60dea4a042c490d73168d41cf70f9cdc3e1e62eb43f8e4"

#define USERID 1200
#define GROUPID 880

#define ADDR_INPUT_REGISTER_START			0x000C 
#define ADDR_HOLDING_REGISTER_START			0x001E
#define ADDR_INPUT_COILS_START				0x00C0 
#define ADDR_OUTPUT_COILS_START				0x01E0

#define CONV_CURRENT 	0
#define CONV_TEMP		1

const json mb_register_map = {
	{{"parameter name", "pump_casing/sealed_medium/pressure"}, 						{"address offset", 19},	{"conversion", CONV_CURRENT}},
	{{"parameter name", "sealing_chamber/barrier_fluid/pressure"}, 					{"address offset", 20},	{"conversion", CONV_CURRENT}},
	{{"parameter name", "sealing_chamber/barrier_fluid/flow"}, 						{"address offset", 21},	{"conversion", CONV_CURRENT}},
	{{"parameter name", "tank/barrier_fluid/level_barrier_fluid"}, 					{"address offset", 22},	{"conversion", CONV_CURRENT}},
	{{"parameter name", "sealing_chamber/barrier_fluid/temp_inlet"}, 				{"address offset", 43},	{"conversion", CONV_TEMP}},
	{{"parameter name", "sealing_chamber/barrier_fluid/temp_outlet"}, 				{"address offset", 44},	{"conversion", CONV_TEMP}},
	{{"parameter name", "water_cooler/cooling_water_barrier_system/temp_inlet"},	{"address offset", 45},	{"conversion", CONV_TEMP}},
	{{"parameter name", "water_cooler/cooling_water_barrier_system/temp_outlet"}, 	{"address offset", 46},	{"conversion", CONV_TEMP}}
};

void check_errorcode(uint16_t offset, int16_t val) {
	if(val >= 32512 || val <= -32512)
		throw rio_exception(ADDR_HOLDING_REGISTER_START + offset, val);
}

int16_t getValue(const uint16_t* start, uint16_t offset) {
	if(start + offset == nullptr)
		throw std::invalid_argument("out of bounds");

	int16_t val = start[offset];
	//int16_t val = ntohs(start[offset]);
	check_errorcode(offset, val);
	
	return val;
}

int32_t getValue32(const uint16_t* start, uint16_t offset) {
	int32_t val = (getValue(start, offset) << 16) + getValue(start, offset + 1);
	return val;
}

float getFloat(const uint16_t* start, uint16_t offset) {
	int32_t data = getValue32(start, offset);
	float val = *reinterpret_cast<float*>(&data);
	return val;
}

template<typename _NumericType = uint16_t>
_NumericType interpolate(double from, double to, double value, _NumericType int_from, _NumericType int_to) {
	return int_from * (1 - (value - from) / (to - from)) + int_to * ((value - from) / (to - from));
}


int main(int argc, char **argv){
	bool daemon = false;
	bool fake = false;

	logfile.setMaxLogLevel(LOG_INFO);

	std::string conn_target = "localhost";
	std::string conn_port = "6450";

	std::string mb_tcp_target = "192.168.2.230";
	int mb_tcp_port = 502;

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
			("mb_tcp_client", "modbus tcp server", cxxopts::value<std::string>(mb_tcp_target)->default_value(mb_tcp_target))
			("mb_tcp_port", "modbus tcp server port", cxxopts::value<int>(mb_tcp_port))
			("fake", "fake data", cxxopts::value<bool>(fake))
		;

		try {
			auto result = options.parse(argc, argv);

			if(result.count("help")) {
				std::cout << options.help() << std::endl;
				return EXIT_SUCCESS;
			}

			if(result.count("version")) {
				std::cout << "bemos-modbus-client version: " << APP_VERSION << std::endl;
				return EXIT_SUCCESS;
			}

			if(daemon) {
				logfile.setEcho(false);
				logfile.write(LOG_INFO, "start daemonized");
			}

			if(result.count("suppress_syslog")) {
				logfile.setEcho(false);
			}

			if(result.count("verbose")) {
				logfile.setMaxLogLevel(LOG_DEBUG);
				logfile.write(LOG_INFO, "verbose output enabled");
			}

			if(result.count("password")) {
				password = bestsens::netHelper::sha512(result["password"].as<std::string>());
			}

			if(fake) {
				logfile.write(LOG_INFO, "fake mode enabled");
			}
		} catch(const std::exception& e) {
			logfile.write(LOG_CRIT, "%s", e.what());
			return EXIT_FAILURE;
		}
	}

	logfile.write(LOG_INFO, "starting bemos-modbus-client %s", APP_VERSION);

	/*
	 * Test IEEE 754
	 */
	if(!std::numeric_limits<float>::is_iec559)
		logfile.write(LOG_WARNING, "application wasn't compiled with IEEE 754 standard, floating point values may be out of standard");

	/*
	 * open socket
	 */
	bestsens::jsonNetHelper socket(conn_target, conn_port);

	/*
	 * connect to socket
	 */
	if(socket.connect()) {
		logfile.write(LOG_CRIT, "connection to BeMoS failed");
		return EXIT_FAILURE;
	}

	/*
	 * login
	 */
	if(!socket.login(username, password)) {
		logfile.write(LOG_CRIT, "login to bemos failed");
		return EXIT_FAILURE;
	}

	modbus_t *ctx = modbus_new_tcp(mb_tcp_target.c_str(), mb_tcp_port);

	if(!ctx) {
		logfile.write(LOG_CRIT, "failed to create modbus context, exiting");
		return EXIT_FAILURE;
	}

	if(!fake) {
		if(modbus_connect(ctx) == -1) {
			logfile.write(LOG_CRIT, "failed to connect to modbus client, exiting");
			modbus_free(ctx);
			return EXIT_FAILURE;
		}
	}

	/* Deamonize */
	if(daemon) {
		bestsens::system_helper::daemonize();
		logfile.write(LOG_INFO, "daemon created");
	} else {
		logfile.write(LOG_DEBUG, "skipped daemonizing");
	}

	bestsens::loopTimer timer(std::chrono::seconds(1), 0);

	/*
	 * register "external_data" algo
	 */
	json j;
	socket.send_command("register_analysis", j, {{"name", "external_data"}});

	bestsens::system_helper::systemd::ready();

	auto temperature_to_register_value = [&](double temperature) -> uint16_t {
		short temp = static_cast<short>(temperature * 10);
		return uint16_t(temp);
	};

	auto register_value_to_temperature = [&](uint16_t value) -> double {
		return double(value) / 10;
	};

	auto convert_register_value_to_current = [&](uint16_t value) -> double {
		return interpolate<double>(0x0000, 0x6C00, value, 4, 20);
	};

	auto convert_current_to_value = [&](double value, double from, double to) -> double {
		return interpolate<double>(4, 20, value, from, to);
	};

	auto convert_value_to_current = [&](double value, double from, double to) -> double {
		return interpolate<double>(from, to, value, 4, 20);
	};

	// pressure in bar
	auto pressure_to_register_value = [&](double pressure) -> uint16_t {
		// convert to current
		double current = convert_value_to_current(pressure, -1, 25); // assume range -1 to 25 bar

		// convert to register value
		return interpolate<uint16_t>(4, 20, current, 0x0000, 0x6C00);
	};

	// flow in l/min
	auto flow_to_register_value = [&](double flow) -> uint16_t {
		// convert to current
		double current = convert_value_to_current(flow, 0, 130*1000/60);

		// convert to register value
		return interpolate<uint16_t>(4, 20, current, 0x0000, 0x6C00);
	};

	auto percentage_to_register_value = [&](double percentage) -> uint16_t {
		// convert to current
		double current = convert_value_to_current(percentage, 0, 1);

		// convert to register value
		return interpolate<uint16_t>(4, 20, current, 0x0000, 0x6C00);
	};

	while(1) {
		timer.wait_on_tick();

		uint16_t reg[128];

		int num;
		if(!fake) {
			num = modbus_read_registers(ctx, ADDR_HOLDING_REGISTER_START, 50, reg);
		} else {
			reg[19] = pressure_to_register_value(5);
			reg[20] = pressure_to_register_value(6);
			reg[21] = flow_to_register_value(15);
			reg[22] = percentage_to_register_value(0.89);

			reg[43] = temperature_to_register_value(50);
			reg[44] = temperature_to_register_value(51);
			reg[45] = temperature_to_register_value(52);
			reg[46] = temperature_to_register_value(53);

			num = 50;
		}
		
		if(num == -1) {
			logfile.write(LOG_CRIT, "error reading registers, exiting: %s", modbus_strerror(errno));
			modbus_close(ctx);
			modbus_free(ctx);
			return EXIT_FAILURE;
		}

		json attribute_data = {
			{"date", time(NULL)}
		};

		for(auto e : mb_register_map) {
			try {
				std::string parameter = e["parameter name"];
				int address_offset = e["address offset"];
				int conversion = e["conversion"];

				uint16_t reg_value = getValue(reg, address_offset);
				double value;

				switch(conversion) {
					case CONV_CURRENT: value = (convert_register_value_to_current(reg_value)-4)/16; break;
					case CONV_TEMP: value = register_value_to_temperature(reg_value); break;
					default: value = reg_value;
				}

				attribute_data[parameter] = value;
			} catch(const std::exception& e) {
				logfile.write(LOG_ERR, "error getting value: %s", e.what());
			}
		}

		const json payload = {
			{"name", "external_data"},
			{"data", attribute_data}
		};

		socket.send_command("new_data", j, payload);

		syslog(LOG_DEBUG, "%s", payload.dump(2).c_str());
	}

	modbus_close(ctx);
	modbus_free(ctx);

	logfile.write(LOG_DEBUG, "exited");

	return EXIT_SUCCESS;
}
