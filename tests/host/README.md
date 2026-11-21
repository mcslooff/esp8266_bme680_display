# Publishing regression tests

Run `bash tests/host/run.sh` with a C++11 compiler (`g++` by default).

Additional CI modes (also documented in the root README):

- `bash tests/host/run.sh --sanitize`: warnings as errors, address/leak and
  undefined-behavior checks; any diagnostic aborts the run.
- `bash tests/host/run.sh --coverage`: warnings as errors and gcovr line/branch
  gates from `gcovr.cfg`. Install `tools/requirements-ci.txt` in a virtual
  environment first. Reports go to `build/coverage/`.

Each mode uses a fresh temporary directory, so stale coverage data cannot mask
a regression. Compiler failures, assertion failures and coverage failures return
a nonzero exit status. Assertions are enabled; these are not release-mode tests.

These tests compile the production publishing classes against small host fakes
for Arduino, asynchronous MQTT and HTTP. They cover transport selection, full
payloads larger than 256 bytes, buffered payload forwarding, credentials,
asynchronous completion/failure, back-off timing/cap/reset, timer rollover,
connection timeout, WiFi gating, duplicate disconnect events, heap headroom,
and success through either enabled transport.
Logging regressions check info-level attempt/back-off/status visibility,
rate limiting and rollover of the status timer, and credential exclusion.
They do not exercise real networking or replace an ESP8266 build and broker
smoke test.

## Device outage/recovery smoke check

After explicitly authorizing/uploading the new firmware:

1. Confirm the ESP has a WiFi IP and measurements reach the Java application's
   broker. Monitor `/dev/ttyUSB0` at 115200 baud.
2. Stop the broker, leaving WiFi available. Check that `/status/json` responds,
   sample count/time advances, and buttons/display work during reconnects.
3. Observe one disconnect/retry log per failure, with delays of 5, 10, 20, 40,
   80, 160 and then at most 300 seconds. A pending attempt is separately capped
   at 30 seconds without waiting in the main loop.
4. Restart the broker. Confirm current and buffered measurements arrive on a
   subsequent successful sampling/publication cycle.
5. Stop the broker again after a successful reconnect and confirm the first
   retry delay is back to 5 seconds.

Also check the listener topic (if configured); incoming payload chunks must
print intact, and the listener is subscribed after each clean connection.

QoS remains 0: async queue acceptance is not a broker delivery acknowledgment.
Physical WiFi conditions, real broker compatibility and sustained operation
must be checked on the device, not inferred from these host fakes.
