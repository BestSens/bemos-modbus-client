#ifndef VERSION_HPP_
#define VERSION_HPP_

#include "gitrev.hpp"

#ifndef APP_VERSION_GITREV
#define APP_VERSION_GITREV
#endif

#define APP_DEV

#define APP_VERSION_MAJOR   1
#define APP_VERSION_MINOR   0
#define APP_VERSION_PATCH   0

#define APP_STR_EXP(__A)    #__A
#define APP_STR(__A)        APP_STR_EXP(__A)

#ifdef APP_DEV
#define APP_VERSION         APP_STR(APP_VERSION_MAJOR) "." APP_STR(APP_VERSION_MINOR) "." APP_STR(APP_VERSION_PATCH) "-dev" APP_STR(APP_VERSION_GITREV)
#else
#define APP_VERSION         APP_STR(APP_VERSION_MAJOR) "." APP_STR(APP_VERSION_MINOR) "." APP_STR(APP_VERSION_PATCH)
#endif

#endif /* VERSION_HPP_ */
