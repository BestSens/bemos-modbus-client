FROM debian:8
RUN apt-get update && apt-get install -y libssl-dev libmodbus-dev

COPY bemos_modbus /usr/bin/bemos_modbus_client
RUN chmod 755 /usr/bin/bemos_modbus_client

ENTRYPOINT ["/usr/bin/bemos_modbus_client"]
