#ifndef VERSION_HPP_
#define VERSION_HPP_

#include <string>

std::string app_version();
std::string app_git_branch();
std::string app_git_revision();
std::string app_compile_date();
std::string app_compile_flags();
std::string app_linker_flags();
std::string app_compiler_version();
bool app_is_dev();

constexpr bool app_is_debug() {
#ifdef DEBUG
	return true;
#else
	return false;
#endif
}

#endif /* VERSION_HPP_ */
