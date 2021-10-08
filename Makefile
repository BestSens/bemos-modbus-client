CPPFLAGS = -std=c++14 -pthread -MMD -MP -I${SDKTARGETSYSROOT}/usr/include/modbus \
			-Iinclude/ -Ilibs/bone_helper/include/ \
			-Ilibs/json/single_include/ -Ilibs/cxxopts/include/ -Ilibs/spdlog/include/ \
			-Ilibs/fmt/include/ -DFMT_HEADER_ONLY -DSPDLOG_FMT_EXTERNAL
LDFLAGS = -lm -lpthread -lcrypto -lmodbus

# Warnings
CPPFLAGS += -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Wno-pragmas

ifndef DEBUG
	CPPFLAGS += -O2 -DNDEBUG
	LDFLAGS += -s
else
	CPPFLAGS += -Og -DDEBUG -g -ggdb3 -rdynamic -funwind-tables -fno-inline
endif

ifdef APP_VERSION_BRANCH
	DAPP_VERSION_BRANCH = -DAPP_VERSION_BRANCH=$(APP_VERSION_BRANCH)
endif

ifdef APP_VERSION_GITREV
	DAPP_VERSION_GITREV = -DAPP_VERSION_GITREV=$(APP_VERSION_GITREV)
endif

BONE_HELPER_PATH = "libs/bone_helper"

OBJ = bemos_modbus_client.o version.o $(BONE_HELPER_PATH)/bone_helper.a
BIN = bemos_modbus_client

DEPFILES := $(OBJ:.o=.d)

$(BIN): $(OBJ)
	$(CXX) $(CPPFLAGS) -o $@ $(OBJ) $(LDFLAGS)

$(OBJ): compiler_flags

systemd: CPPFLAGS += -DENABLE_SYSTEMD_STATUS
systemd: LDFLAGS += -lsystemd
systemd: $(BIN)

.PHONY: systemd clean force gitrev.hpp

compiler_flags: force
	echo '$(CPPFLAGS)' | cmp -s - $@ || echo '$(CPPFLAGS)' > $@

gitrev.hpp:
	@echo "#ifndef APP_VERSION_GITREV" > $@
	@echo -n "#define APP_VERSION_GITREV " >> $@
	@git rev-parse --verify --short=8 HEAD >> $@
	@echo "#endif" >> $@
	@echo "#ifndef APP_VERSION_BRANCH" >> $@
	@echo -n "#define APP_VERSION_BRANCH " >> $@
	@git rev-parse --abbrev-ref HEAD >> $@
	@echo "#endif" >> $@

version.o: src/version.cpp gitrev.hpp
	$(CXX) -c $(CPPFLAGS) $(DAPP_VERSION_BRANCH) $(DAPP_VERSION_GITREV) -DCPPFLAGS="$(CXX) -c $(CPPFLAGS)" -DLDFLAGS="$(LDFLAGS)" $< -o $@

%.o: src/%.cpp
	$(CXX) -c $(CPPFLAGS) $< -o $@

$(BONE_HELPER_PATH)/bone_helper.a:
	$(MAKE) -C $(BONE_HELPER_PATH) DEBUG=$(DEBUG) MUTE_WARNINGS=true

-include $(DEPFILES)

clean:
	rm -f $(BIN) $(OBJ) gitrev.hpp compiler_flags
	$(MAKE) clean -C $(BONE_HELPER_PATH)
