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
#include <modbus.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include "version.hpp"
#include "libs/cxxopts/include/cxxopts.hpp"
#include "libs/json/single_include/nlohmann/json.hpp"
#include "libs/bone_helper/netHelper.hpp"
#include "libs/bone_helper/system_helper.hpp"

using namespace bestsens;

system_helper::LogManager logfile("bemos-modbus-client");

#define LOGIN_USER "bemos-analysis"
#define LOGIN_HASH "82e324d4dac1dacf019e498d6045835b3998def1c1cece4abf94a3743f149e208f30276b3275fdbb8c60dea4a042c490d73168d41cf70f9cdc3e1e62eb43f8e4"

#define USERID 1200
#define GROUPID 880

int main(int argc, char **argv){
	modbus_mapping_t *mb_mapping;
	uint8_t *query;
	modbus_t *ctx;
	int s = -1;
	int rc;

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
		cxxopts::Options options("bemos-modbus", "BeMoS one modbus application");

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
				std::cout << "bemos-modbus version: " << APP_VERSION << std::endl;
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

	logfile.write(LOG_INFO, "starting bemos-modbus %s", APP_VERSION);

	/*
	 * Test IEEE 754
	 */
	if(!std::numeric_limits<float>::is_iec559)
		logfile.write(LOG_WARNING, "application wasn't compiled with IEEE 754 standard, floating point values may be out of standard");

	/*
	 * open socket
	 */
	bestsens::jsonNetHelper * socket = new bestsens::jsonNetHelper(conn_target, conn_port);

	/*
	 * connect to socket
	 */
	if(socket->connect()) {
		logfile.write(LOG_CRIT, "connection failed");
		return EXIT_FAILURE;
	}

	/*
	 * login if enabled
	 */
	if(!socket->login(username, password)) {
		logfile.write(LOG_CRIT, "login failed");
		return EXIT_FAILURE;
	}

	ctx = modbus_new_tcp("127.0.0.1", port);
	query = (uint8_t*)malloc(MODBUS_TCP_MAX_ADU_LENGTH);
	//int header_length = modbus_get_header_length(ctx);

	mb_mapping = modbus_mapping_new(0, 0, 10, 50);

	if (mb_mapping == NULL) {
		logfile.write(LOG_CRIT, "Failed to allocate the mapping: %s", modbus_strerror(errno));
		modbus_free(ctx);
		return EXIT_FAILURE;
	}

	s = modbus_tcp_listen(ctx, 1);

	if(s == -1) {
		logfile.write(LOG_CRIT, "cannot reserve port %d, exiting", port);
		modbus_mapping_free(mb_mapping);
		free(query);
		/* For RTU */
		modbus_close(ctx);
		modbus_free(ctx);
		return EXIT_FAILURE;
	}

	logfile.write(LOG_INFO, "listening on port %d", port);

	if(getuid() == 0) {
		/* process is running as root, drop privileges */
		logfile.write(LOG_INFO, "running as root, dropping privileges");

		if(setgid(GROUPID) != 0)
			logfile.write(LOG_ERR, "setgid: Unable to drop group privileges: %s", strerror(errno));
		if(setuid(USERID) != 0)
			logfile.write(LOG_ERR, "setuid: Unable to drop user privileges: %s", strerror(errno));
	}

	/* Deamonize */
	if(daemon) {
		bestsens::system_helper::daemonize();
		logfile.write(LOG_INFO, "daemon created");
	} else {
		logfile.write(LOG_DEBUG, "skipped daemonizing");
	}

	bestsens::system_helper::systemd::ready();

	while(1) {
		modbus_tcp_accept(ctx, &s);

		logfile.write(LOG_DEBUG, "client connected");

		/*
		 * register "external_data" algo
		 */
		json j;
		socket->send_command("register_analysis", j, {{"name", "external_data"}});

		std::cout << std::setw(2) << j << std::endl;

		while(1) {
			auto addValue = [&mb_mapping](uint16_t address, const json& source, const std::string& value) {
				uint16_t response = 0;

				try {
					response = source["payload"].value(value, 0);
				} catch(...) {
					response = 0;
				}

				mb_mapping->tab_input_registers[address] = htons(response);
			};

			auto addValue32 = [&mb_mapping](uint16_t address_start, const json& source, const std::string& value) {
				uint32_t response = 0;

				try {
					response = source["payload"].value(value, 0);
				} catch(const json::exception& e) {
					response = 0;
				}

				response = htonl(response);

				mb_mapping->tab_input_registers[address_start] = (uint16_t)response;
				mb_mapping->tab_input_registers[address_start+1] = (uint16_t)(response >> 16);
			};

			auto addFloat = [&mb_mapping](uint16_t address_start, const json& source, const std::string& value) {
				float response = 0.0;

				try {
					response = source["payload"].value(value, 0.0);
				} catch(const json::exception& e) {
					response = 0.0;
				}

				uint16_t* buff = reinterpret_cast<uint16_t*>(&response);

				mb_mapping->tab_input_registers[address_start] = htons(buff[1]);
				mb_mapping->tab_input_registers[address_start+1] = htons(buff[0]);
			};

			do {
				rc = modbus_receive(ctx, query);
				/* Filtered queries return 0 */
			} while (rc == 0);

			if (rc == -1 && errno != EMBBADCRC) {
				/* Quit */
				break;
			}

			/*
			 * get channel_data
			 */
			json channel_data;

			if(socket->send_command("channel_data", channel_data)) {
				logfile.write(LOG_DEBUG, "%s", channel_data.dump(2).c_str());

				addValue32( 1,	channel_data, "date");
				addFloat(   3,	channel_data, "cage speed");
				addFloat(   5,	channel_data, "shaft speed");
				addFloat(   7,	channel_data, "temp mean");
				addFloat(   9,	channel_data, "stoerlevel");
				addFloat(   11,	channel_data, "mean rt");
				addFloat(   13,	channel_data, "mean amp");
				addFloat(   15,	channel_data, "rms rt");
				addFloat(   17,	channel_data, "rms amp");
				addFloat(   19,	channel_data, "temp0");
				addFloat(   21,	channel_data, "temp1");
				addFloat(   23,	channel_data, "druckwinkel");
			}

			/*
			 * get axial_force
			 */
			json axial_force;

			if(socket->send_command("channel_data", axial_force, {{"name", "axial_force"}})) {
				logfile.write(LOG_DEBUG, "%s", axial_force.dump(2).c_str());
				addFloat(   25,	axial_force, "axial_foce");
			}

			uint16_t external_shaft_speed = ntohs(mb_mapping->tab_registers[1]);

			json payload = {
				{"name", "external_data"},
				{"data", {
					{"shaft_speed", external_shaft_speed}
				}}
			};

			logfile.write(LOG_DEBUG, "updating shaft speed %s", payload.dump(2).c_str());

			socket->send_command("new_data", j, payload);

			rc = modbus_reply(ctx, query, rc, mb_mapping);
			if (rc == -1) {
				break;
			}
		}

		logfile.write(LOG_DEBUG, "client disconnected");
	}

	close(s);
	modbus_mapping_free(mb_mapping);
	free(query);
	/* For RTU */
	modbus_close(ctx);
	modbus_free(ctx);

	logfile.write(LOG_DEBUG, "exited");

	return EXIT_SUCCESS;
}
