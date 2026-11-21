# Publishing regression tests

Run `bash tests/host/run.sh` with a C++11 compiler (`g++` by default).

These tests compile the production publishing classes against small host fakes
for Arduino, MQTT and HTTP. They cover transport selection, full payloads larger
than 256 bytes, buffered payload forwarding, connection reuse/reconnect,
credentials, MQTT failure logs and the existing either-transport success rule.
They do not exercise real networking or replace an ESP8266 build and broker
smoke test.
