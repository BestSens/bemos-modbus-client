cmake_minimum_required(VERSION 3.24)

project(bemos-modbus-client VERSION 2.1.2)

set(MAIN_EXECUTABLE bemos_modbus_client)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libs/bone_helper/cpm")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libs/bone_helper/cmake")

set(Boost_USE_STATIC_LIBS ON)

include(pre)

option(USE_SYSTEM_SSL "Use system OpenSSL instead of bundled" ON)

if(NOT USE_SYSTEM_SSL)
	include(openssl)
endif()

include(fmt)
include(spdlog)
include(bone_helper)
include(add_cxxopts)
include(json)

if(BUILD_TESTS)
	include(catch)
endif()

add_executable(${MAIN_EXECUTABLE}
	src/bemos_modbus_client.cpp
)

add_library(version src/version.cpp)
add_dependencies(version version_header)

find_package(PkgConfig)
pkg_check_modules(modbus REQUIRED "libmodbus")

target_include_directories(${MAIN_EXECUTABLE} PRIVATE
	include
	${modbus_INCLUDE_DIRS}
)

target_include_directories(version PRIVATE include)

target_link_libraries(version PRIVATE fmt common_compile_options)
target_link_libraries(${MAIN_EXECUTABLE} PRIVATE
	common_compile_options
	version
	fmt
	spdlog
	bone_helper
	cxxopts
	nlohmann_json::nlohmann_json
	${modbus_LINK_LIBRARIES}
)

include(post)
