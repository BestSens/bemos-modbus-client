/*
 * version.cpp
 *
 *  Created on: 17.04.2018
 *      Author: Jan Schöppach
 */

#include "version.hpp"
#include <string>
#include <cctype>
#include "gitrev.hpp"
#include "version_info.hpp"

#define APP_STR_EXP(__A)	#__A
#define APP_STR(__A)		APP_STR_EXP(__A)

const std::string version = std::string(APP_STR(APP_VERSION_MAJOR)) + "." + 
							std::string(APP_STR(APP_VERSION_MINOR)) + "." + 
							std::string(APP_STR(APP_VERSION_PATCH));
const std::string branch = std::string(APP_STR(APP_VERSION_BRANCH));
const std::string revision = std::string(APP_STR(APP_VERSION_GITREV));

bool app_is_dev() {
	return branch != "master" && !std::isdigit(branch[0]);
}

std::string app_version() {
	if(app_is_dev()) {
		if(app_is_debug())
			return version + "-" + branch + revision + "-dbg";
		else
			return version + "-" + branch + revision;
	}

	return version;
}

std::string app_git_branch() {
	return branch;
}

std::string app_git_revision() {
	return revision;
}

std::string app_compile_date() {
	return std::string(__TIMESTAMP__);
}

std::string app_compile_flags() {
	return std::string(APP_STR(CPPFLAGS));
}

std::string app_linker_flags() {
	return std::string(APP_STR(LDFLAGS));
}

std::string app_compiler_version() {
	return std::string(__VERSION__);
}