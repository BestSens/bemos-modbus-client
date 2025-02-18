## 2.1.0 (18.02.2025)
- restructure for new build toolchain
- fix crash on empty map in configuration file
- update nlohmann/json library to v3.11.3 (https://github.com/nlohmann/json/releases/tag/v3.11.3)
- update fmt library to v11.0.2 (https://github.com/fmtlib/fmt/releases/tag/11.0.2)
- update spdlog library to v1.15.0 (https://github.com/gabime/spdlog/releases/tag/v1.15.0)
- slave id configuration option is now also used with Modbus TCP

## 2.0.0 (12.10.2021)
- use spdlog as logging library
- make generic to be used in parallel with configuration files
- client now registers data_sources if available in server and defined in configuration
- update nlohmann/json library to v3.10.3 (https://github.com/nlohmann/json/releases/tag/v3.10.3)
- update fmt library to v8.0.1 (https://github.com/fmtlib/fmt/releases/tag/8.0.1)
- update spdlog library to v1.9.1 (https://github.com/gabime/spdlog/releases/tag/v1.9.1)

## 1.0.2 (20.11.2020)
- no code changes

## 1.0.1 (20.11.2020)
- updated build handling
- update nlohmann/json library to v3.9.1 (https://github.com/nlohmann/json/releases/tag/v3.9.1)
- add support for libmodbus 3.1.6