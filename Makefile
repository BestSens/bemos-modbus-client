CPPFLAGS = -std=c++14 -pthread -MMD -MP -I${SDKTARGETSYSROOT}/usr/include/modbus
LDFLAGS = -lm -lpthread -lcrypto -lmodbus

ifndef DEBUG
	CPPFLAGS += -O2 -DNDEBUG
else
	CPPFLAGS += -O1 -DDEBUG -Wall -g -rdynamic -funwind-tables -fno-inline
endif

ifdef APP_VERSION_BRANCH
	DAPP_VERSION_BRANCH = -DAPP_VERSION_BRANCH=$(APP_VERSION_BRANCH)
endif

ifdef APP_VERSION_GITREV
	DAPP_VERSION_GITREV = -DAPP_VERSION_GITREV=$(APP_VERSION_GITREV)
endif

OBJ = bemos_modbus_client.o version.o
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

version.o: version.cpp gitrev.hpp
	$(CXX) -c $(CPPFLAGS) $(DAPP_VERSION_BRANCH) $(DAPP_VERSION_GITREV) -DCPPFLAGS="$(CXX) -c $(CPPFLAGS)" -DLDFLAGS="$(LDFLAGS)" $< -o $@

%.o: %.cpp
	$(CXX) -c $(CPPFLAGS) $< -o $@

-include $(DEPFILES)

clean:
	rm -f $(BIN) $(OBJ) gitrev.hpp compiler_flags
