#ifndef VERSION_HPP_
#define VERSION_HPP_

#include <string>

auto app_version() -> std::string;
auto app_git_branch() -> std::string;
auto app_git_revision() -> std::string;
auto app_compile_date() -> std::string;
auto app_compile_flags() -> std::string;
auto app_linker_flags() -> std::string;
auto app_compiler_version() -> std::string;
constexpr auto app_is_dev() -> bool;

constexpr auto app_is_debug() -> bool {
#ifdef DEBUG
	return true;
#else
	return false;
#endif
}

#endif /* VERSION_HPP_ */
