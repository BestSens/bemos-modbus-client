/*
 * version.cpp
 *
 *  Created on: 17.04.2018
 *		Author: Jan Schöppach
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
	constexpr auto branch = APP_STR(APP_VERSION_BRANCH);
	constexpr auto revision = APP_STR(APP_VERSION_GITREV);
}

constexpr auto strings_equal(const char * a, const char * b) -> bool {
	return *a == *b && (*a == '\0' || strings_equal(a + 1, b + 1));
}

constexpr auto app_is_dev() -> bool {
	return !strings_equal(branch, "master") && (std::isdigit(branch[0]) == 0);
}

auto app_version() -> std::string {
	static const std::string version = fmt::format("{}.{}.{}", APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH);

	if(app_is_dev()) {
		if(app_is_debug())
			return version + "-" + std::string(branch) + std::string(revision) + "-dbg";
		else
			return version + "-" + std::string(branch) + std::string(revision);
	}

	return version;
}

auto app_git_branch() -> std::string {
	return {branch};
}

auto app_git_revision() -> std::string {
	return {revision};
}

auto app_compile_date() -> std::string {
	return {__TIMESTAMP__};
}

auto app_compile_flags() -> std::string {
	return {APP_STR(CPPFLAGS)};
}

auto app_linker_flags() -> std::string {
	return {APP_STR(LDFLAGS)};
}

auto app_compiler_version() -> std::string {
	return {__VERSION__};
}
