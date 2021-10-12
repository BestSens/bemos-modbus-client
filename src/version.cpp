/*
 * version.cpp
 *
 *  Created on: 17.04.2018
 *      Author: Jan Schöppach
 */

#include "bemos_modbus_client/version.hpp"

#include <cctype>
#include <string>

#include "bemos_modbus_client/version_info.hpp"
#include "fmt/format.h"
#include "../gitrev.hpp"

#define APP_STR_EXP(__A)	#__A
#define APP_STR(__A)		APP_STR_EXP(__A)

namespace {
	const std::string version = fmt::format("{}.{}.{}", app_version_major, app_version_minor, app_version_patch);

	constexpr auto branch = APP_STR(APP_VERSION_BRANCH);
	constexpr auto revision = APP_STR(APP_VERSION_GITREV);
}

constexpr auto stringsEqual(const char * a, const char * b) -> bool {
    return *a == *b && (*a == '\0' || stringsEqual(a + 1, b + 1));
}

constexpr auto appIsDev() -> bool {
	return !stringsEqual(branch, "master") && (std::isdigit(branch[0]) == 0);
}

auto appVersion() -> std::string {
	if(appIsDev()) {
		if(appIsDebug())
			return version + "-" + std::string(branch) + std::string(revision) + "-dbg";
		else
			return version + "-" + std::string(branch) + std::string(revision);
	}

	return version;
}

auto appGitBranch() -> std::string {
	return std::string(branch);
}

auto appGitRevision() -> std::string {
	return std::string(revision);
}

auto appCompileDate() -> std::string {
	return std::string(__TIMESTAMP__);
}

auto appCompileFlags() -> std::string {
	return std::string(APP_STR(CPPFLAGS));
}

auto appLinkerFlags() -> std::string {
	return std::string(APP_STR(LDFLAGS));
}

auto appCompilerVersion() -> std::string {
	return std::string(__VERSION__);
}
