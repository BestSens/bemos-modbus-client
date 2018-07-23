CFLAGS = -std=c11 -D_XOPEN_SOURCE=700 -DNDEBUG
CPPFLAGS = -std=c++14 -DNDEBUG -I/usr/include/modbus
LDFLAGS = -lm -lpthread -lcrypto -lmodbus

OBJ = bemos_modbus_client.o
BIN = bemos_modbus_client

all: $(BIN)

debug: CFLAGS = -std=c11 -D_XOPEN_SOURCE=700 -DDEBUG -O0 -Wall -g
debug: CPPFLAGS = -std=c++14 -DDEBUG -O0 -Wall -g
debug: $(BIN)

systemd: CFLAGS += -DENABLE_SYSTEMD_STATUS
systemd: CPPFLAGS += -DENABLE_SYSTEMD_STATUS
systemd: LDFLAGS += -lsystemd
systemd: $(BIN)

.PHONY: clean

$(BIN): $(OBJ)
	$(CXX) $(CPPFLAGS) -o $@ $(OBJ) $(LDFLAGS)

gitrev.hpp: FORCE
	@echo -n "#define APP_VERSION_GITREV " > $@
	@git rev-parse --verify --short=8 HEAD >> $@

FORCE:

gitrev.hpp.md5: gitrev.hpp
	@md5sum $< | cmp -s $@ -; if test $$? -ne 0; then md5sum $< > $@; fi

bemos_modbus_client.o: bemos_modbus_client.cpp version.hpp libs/bone_helper/system_helper.hpp libs/json/single_include/nlohmann/json.hpp libs/cxxopts/include/cxxopts.hpp gitrev.hpp.md5
	$(CXX) $(CPPFLAGS) -c $<

clean:
	rm -f $(BIN) $(OBJ) gitrev.hpp gitrev.hpp.md5
