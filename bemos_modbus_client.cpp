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
#include "libs/cxxopts/include/cxxopts.hpp"
#include "libs/bone_helper/loopTimer.hpp"
#include "libs/json/single_include/nlohmann/json.hpp"
#include "libs/bone_helper/netHelper.hpp"
#include "libs/bone_helper/system_helper.hpp"

using namespace bestsens;

system_helper::LogManager logfile("bemos-modbus-client");

#define LOGIN_USER "bemos-analysis"
#define LOGIN_HASH "82e324d4dac1dacf019e498d6045835b3998def1c1cece4abf94a3743f149e208f30276b3275fdbb8c60dea4a042c490d73168d41cf70f9cdc3e1e62eb43f8e4"

#define USERID 1200
#define GROUPID 880

uint16_t getValue(const uint16_t* start) {
	if(start == nullptr)
		throw std::invalid_argument("error out of bounds");
	
	return ntohs(start[0]);
}

uint32_t getValue32(const uint16_t* start) {
	return (getValue(start) << 16) + getValue(start + 1);
}

float getFloat(const uint16_t* start) {
	uint32_t data = getValue32(start);
	return *reinterpret_cast<float*>(&data);
}

int main(int argc, char **argv){
	bool daemon = false;
	int port = 502;

	logfile.setMaxLogLevel(LOG_INFO);

	std::string conn_target = "localhost";
	std::string conn_port = "6450";
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
			("o,listen", "modbus tcp listen port", cxxopts::value<int>(port))
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

	modbus_t *ctx = modbus_new_tcp("192.168.2.230", port);

	if(!ctx) {
		logfile.write(LOG_CRIT, "failed to create modbus context, exiting");
		return EXIT_FAILURE;
	}

	if(modbus_connect(ctx) == -1) {
		logfile.write(LOG_CRIT, "failed to connect to modbus client, exiting");
		modbus_free(ctx);
		return EXIT_FAILURE;
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
	socket.send_command("register_analysis", j, {{"name", "sensor_data"}});

	bestsens::system_helper::systemd::ready();

	while(1) {
		timer.wait_on_tick();

		uint16_t reg[27];

		int num = modbus_read_input_registers(ctx, 0, 27, reg);

		if(num == -1) {
			logfile.write(LOG_CRIT, "error reading registers, exiting: %s", modbus_strerror(errno));
			break;
		}

		int date = getValue32(reg + 1);
		float cage_speed = getFloat(reg + 3);
		float shaft_speed = getFloat(reg + 5);
		float temp = getFloat(reg + 7);

		logfile.write(LOG_INFO, "date: %d", date);
		logfile.write(LOG_INFO, "temp: %.2f", temp);
		logfile.write(LOG_INFO, "cage_speed: %.2f", cage_speed);
		logfile.write(LOG_INFO, "shaft_speed: %.2f", shaft_speed);

		const json payload = {
			{"name", "sensor_data"},
			{"data", {
				{"date", date},
				{"temp", temp},
				{"cage_speed", cage_speed},
				{"shaft_speed", shaft_speed}
			}}
		};

		logfile.write(LOG_INFO, "updating sensor data: %s", payload.dump(2).c_str());

		socket.send_command("new_data", j, payload);
	}

	modbus_close(ctx);
	modbus_free(ctx);

	logfile.write(LOG_DEBUG, "exited");

	return EXIT_SUCCESS;
}
