#ifndef VERSION_HPP_
#define VERSION_HPP_

#include <string>

auto appVersion() -> std::string;
auto appGitBranch() -> std::string;
auto appGitRevision() -> std::string;
auto appCompileDate() -> std::string;
auto appCompilerVersion() -> std::string;
auto appIsDev() -> bool;

constexpr auto appIsDebug() -> bool {
#ifdef DEBUG
	return true;
#else
	return false;
#endif
}

#endif /* VERSION_HPP_ */
