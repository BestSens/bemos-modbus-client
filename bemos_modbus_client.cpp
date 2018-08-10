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

system_helper::LogManager logfile("bemos-modbus-client");

#define LOGIN_USER "bemos-analysis"
#define LOGIN_HASH "82e324d4dac1dacf019e498d6045835b3998def1c1cece4abf94a3743f149e208f30276b3275fdbb8c60dea4a042c490d73168d41cf70f9cdc3e1e62eb43f8e4"

#define USERID 1200
#define GROUPID 880

void check_errorcode(uint16_t offset, int16_t val) {
	if(val >= 32512 || val <= -32512)
		throw rio_exception(offset, val);
}

int16_t getValue(const uint16_t* start, uint16_t offset) {
	if(start + offset == nullptr)
		throw std::invalid_argument("out of bounds");

	int16_t val = ntohs(start[offset]);
	check_errorcode(offset, val);
	
	return val;
}

int32_t getValue32(const uint16_t* start, uint16_t offset) {
	int32_t val;

	try{
		val = (getValue(start, offset) << 16) + getValue(start, offset + 1);
	} catch(const std::exception& e) {
		logfile.write(LOG_ERR, "error getting variable: %s", e.what());
		val = 0;
	}

	return val;
}

float getFloat(const uint16_t* start, uint16_t offset) {
	int32_t data = getValue32(start, offset);
	float val;

	try {
		val = *reinterpret_cast<float*>(&data);
	} catch(const std::exception& e) {
		logfile.write(LOG_ERR, "error getting variable: %s", e.what());
		val = NAN;
	}
	
	return val;
}

int main(int argc, char **argv){
	bool daemon = false;

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

	modbus_t *ctx = modbus_new_tcp(mb_tcp_target.c_str(), mb_tcp_port);

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
	socket.send_command("register_analysis", j, {{"name", "external_data"}});

	bestsens::system_helper::systemd::ready();

	while(1) {
		timer.wait_on_tick();

		uint16_t reg[29];

		int num = modbus_read_input_registers(ctx, 0, 29, reg);

		if(num == -1) {
			logfile.write(LOG_CRIT, "error reading registers, exiting: %s", modbus_strerror(errno));
			modbus_close(ctx);
			modbus_free(ctx);
			return EXIT_FAILURE;
		}

		float temp = getFloat(reg, 7);
		float vibration = getFloat(reg, 27);

		const json payload = {
			{"name", "external_data"},
			{"data", {
				{"temp", temp},
				{"vibration", vibration}
			}}
		};

		socket.send_command("new_data", j, payload);

		syslog(LOG_DEBUG, "%s", payload.dump(2).c_str());
	}

	modbus_close(ctx);
	modbus_free(ctx);

	logfile.write(LOG_DEBUG, "exited");

	return EXIT_SUCCESS;
}
