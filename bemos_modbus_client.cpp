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
using json = nlohmann::json;

system_helper::LogManager logfile("bemos-modbus-client");

#define LOGIN_USER "bemos-analysis"
#define LOGIN_HASH "82e324d4dac1dacf019e498d6045835b3998def1c1cece4abf94a3743f149e208f30276b3275fdbb8c60dea4a042c490d73168d41cf70f9cdc3e1e62eb43f8e4"

#define USERID 1200
#define GROUPID 880

#define ADDR_INPUT_REGISTER_START	0x0000 

const json mb_register_map = {
	{{"parameter name", "filtered_adc"}, 		{"address offset", 10}},
	{{"parameter name", "electrical_value"}, 	{"address offset", 12}},
	{{"parameter name", "gross"}, 				{"address offset", 14}},
	{{"parameter name", "net"}, 				{"address offset", 16}},
	{{"parameter name", "minimum"}, 			{"address offset", 18}},
	{{"parameter name", "maximum"}, 			{"address offset", 20}},
	{{"parameter name", "peak_to_peak"}, 		{"address offset", 22}}
};


int16_t getValue(const uint16_t* start, uint16_t offset) {
	if(start + offset == nullptr)
		throw std::invalid_argument("out of bounds");

	int16_t val = start[offset];
	
	return val;
}

int32_t getValue32(const uint16_t* start, uint16_t offset) {
	int32_t val = (getValue(start, offset) << 16) + getValue(start, offset + 1);
	return val;
}

float getFloat(const uint16_t* start, uint16_t offset) {
	if(start + offset == nullptr)
		throw std::invalid_argument("out of bounds");

	//syslog(LOG_DEBUG, "abcd: %f", modbus_get_float_abcd(start + offset));
	//syslog(LOG_DEBUG, "cdab: %f", modbus_get_float_cdab(start + offset));
	//syslog(LOG_DEBUG, "badc: %f", modbus_get_float_badc(start + offset));
	//syslog(LOG_DEBUG, "dcba: %f", modbus_get_float_dcba(start + offset));

	return modbus_get_float_cdab(start + offset);
}

template<typename _NumericType = uint16_t>
_NumericType interpolate(double from, double to, double value, _NumericType int_from, _NumericType int_to) {
	return int_from * (1 - (value - from) / (to - from)) + int_to * ((value - from) / (to - from));
}


int main(int argc, char **argv){
	bool daemon = false;
	bool skip_bemos = false;

	logfile.setMaxLogLevel(LOG_INFO);

	std::string conn_target = "localhost";
	std::string conn_port = "6450";

	std::string mb_protocol = "tcp";
	double mb_timeout = 1.0;
	std::string mb_tcp_target = "192.168.2.230";
	int mb_tcp_port = 502;
	
	std::string mb_rtu_serialport = "/dev/ttyS1";
	int mb_rtu_baud = 9600;
	std::string mb_rtu_parity = "N";
	int mb_rtu_databits = 8;
	int mb_rtu_stopbits = 1;
	int mb_rtu_slave = 1;

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
			("mb_timeout", "modbus response timeout in seconds", cxxopts::value<double>(mb_timeout)->default_value(std::to_string(mb_timeout)))
			("mb_rtu_serialport", "modbus serial port", cxxopts::value<std::string>(mb_rtu_serialport))
			("mb_rtu_baud", "modbus serial baudrate", cxxopts::value<int>(mb_rtu_baud)->default_value(std::to_string(mb_rtu_baud)))
			("mb_rtu_parity", "modbus serial parity: none (N), even (E), odd (O)", cxxopts::value<std::string>(mb_rtu_parity)->default_value(mb_rtu_parity))
			("mb_rtu_databits", "modbus serial data bits", cxxopts::value<int>(mb_rtu_databits)->default_value(std::to_string(mb_rtu_databits)))
			("mb_rtu_stopbits", "modbus serial stop bits", cxxopts::value<int>(mb_rtu_stopbits)->default_value(std::to_string(mb_rtu_stopbits)))
			("mb_rtu_slave", "modbus serial slave address", cxxopts::value<int>(mb_rtu_slave)->default_value(std::to_string(mb_rtu_slave)))
			("mb_protocol", "can be either: rtu or tcp", cxxopts::value<std::string>(mb_protocol)->default_value(mb_protocol))
			("skip_bemos", "do not use bemos", cxxopts::value<bool>(skip_bemos))
		;

		try {
			auto result = options.parse(argc, argv);

			if(result.count("help")) {
				std::cout << options.help() << std::endl;
				return EXIT_SUCCESS;
			}

			if(result.count("version")) {
				std::cout << "bemos-modbus-client version: " << app_version() << std::endl;

				if(result.count("verbose")) {
					std::cout << "git branch: " << app_git_branch() << std::endl;
					std::cout << "git revision: " << app_git_revision() << std::endl;
					std::cout << "compiled @ " << app_compile_date() << std::endl;
					std::cout << "compiler version: " << app_compiler_version() << std::endl;
					std::cout << "compiler flags: " << app_compile_flags() << std::endl;
					std::cout << "linker flags: " << app_linker_flags() << std::endl;
				}

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

			if(skip_bemos)
			{
				logfile.write(LOG_INFO, "don't use bemos");
			}

			// todo: check modbus rtu configuration
			if(mb_protocol.compare("tcp") == 0)
			{
				logfile.write(LOG_INFO, "use modbus_tcp with ip = %s & port = %i", mb_tcp_target.c_str(), mb_tcp_port);
			}

			if(mb_protocol.compare("rtu") == 0)
			{
				logfile.write(LOG_INFO, "use modbus_rtu with device = %s, baudrate = %i, parity = %s, data bits = %i, stop bits = %i & slave address = %i", mb_rtu_serialport.c_str(), mb_rtu_baud, mb_rtu_parity.c_str(), mb_rtu_databits, mb_rtu_stopbits, mb_rtu_slave);
			}
		} catch(const std::exception& e) {
			logfile.write(LOG_CRIT, "%s", e.what());
			return EXIT_FAILURE;
		}
	}

	logfile.write(LOG_INFO, "starting bemos-modbus-client %s", app_version().c_str());

	/*
	 * Test IEEE 754
	 */
	if(!std::numeric_limits<float>::is_iec559)
		logfile.write(LOG_WARNING, "application wasn't compiled with IEEE 754 standard, floating point values may be out of standard");

	/*
	 * open socket
	 */
	bestsens::jsonNetHelper* socket;

	if(!skip_bemos)
	{	
		socket = new bestsens::jsonNetHelper(conn_target, conn_port);

		/*
		 * connect to socket
		 */
		if(socket->connect()) {
			logfile.write(LOG_CRIT, "connection to BeMoS failed");
			return EXIT_FAILURE;
		}

		/*
		 * login
		 */
		if(!socket->login(username, password)) {
			logfile.write(LOG_CRIT, "login to bemos failed");
			return EXIT_FAILURE;
		}
	}

	modbus_t *ctx;
	if(mb_protocol.compare("tcp") == 0)
		ctx = modbus_new_tcp(mb_tcp_target.c_str(), mb_tcp_port);
	else
		ctx = modbus_new_rtu(mb_rtu_serialport.c_str(), mb_rtu_baud, mb_rtu_parity.front(), mb_rtu_databits, mb_rtu_stopbits);

	if(!ctx) {
		logfile.write(LOG_CRIT, "failed to create modbus context, exiting");
		return EXIT_FAILURE;
	}

	/*
	 * set modbus slave address
	 */
	if(0 != modbus_set_slave(ctx, mb_rtu_slave))
	{
		logfile.write(LOG_CRIT, "could not set slave address to %i", mb_rtu_slave);
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
		logfile.write(LOG_CRIT, "error setting modbus timeout");
		return EXIT_FAILURE;
	}
#else
	modbus_set_response_timeout(ctx, &mb_timeout_t);
#endif

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

	json j;
	if(!skip_bemos)
	{
		/*
		 * register "clipx" algo
		 */
		socket->send_command("register_analysis", j, {{"name", "clipx"}});
	}

	bestsens::system_helper::systemd::ready();

	while(1) {
		bestsens::system_helper::systemd::watchdog();
		timer.wait_on_tick();

		uint16_t reg[128];

		int num = modbus_read_input_registers(ctx, ADDR_INPUT_REGISTER_START, 24, reg + ADDR_INPUT_REGISTER_START);
		
		if(num == -1) {
			logfile.write(LOG_CRIT, "error reading registers, exiting: %s", modbus_strerror(errno));
			modbus_close(ctx);
			modbus_free(ctx);
			return EXIT_FAILURE;
		}

		json attribute_data = {
			{"date", std::time(nullptr)}
		};

		for(auto e : mb_register_map) {
			try {
				std::string parameter = e["parameter name"];
				int address_offset = e["address offset"];

				attribute_data[parameter] = getFloat(reg, address_offset);
			} catch(const std::exception& err) {
				logfile.write(LOG_ERR, "error getting value: %s", err.what());
			}
		}

		const json payload = {
			{"name", "clipx"},
			{"data", attribute_data}
		};

		if(!skip_bemos) {	
			socket->send_command("new_data", j, payload);
		}

		syslog(LOG_DEBUG, "%s", payload.dump(2).c_str());
	}

	modbus_close(ctx);
	modbus_free(ctx);

	delete(socket);

	logfile.write(LOG_DEBUG, "exited");

	return EXIT_SUCCESS;
}
