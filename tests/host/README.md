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
for Arduino, asynchronous TCP/MQTT and a small in-memory filesystem. They cover transport selection, full
payloads larger than 256 bytes, buffered payload forwarding, credentials,
asynchronous completion/failure, back-off timing/cap/reset, timer rollover,
connection timeout, WiFi gating, duplicate disconnect events, heap headroom,
and success through either enabled transport.
Logging regressions check info-level attempt/back-off/status visibility,
rate limiting and rollover of the status timer, and credential exclusion.
They do not exercise real networking or replace an ESP8266 build and broker
smoke test.

The HTTP/replay regressions additionally cover partial sends, TCP backpressure,
split/interim/malformed HTTP responses, Basic auth and URL handling, total
timeout/cooldown with clock rollover, and rejection without exposing secrets.
Queue integration tests retain failed HTTP records, accept samples while a
request is stalled, enforce the per-step read/publication budget, and exercise
buffer capacity/partial writes, recovery and reset-safe original-file retention.

Serializer regressions run in every mode: the C++ binary emits synthetic
measurement fixtures and `check_serialized_json.py` parses them with Python's
standard JSON parser, validating identity, timestamp, version, selected values,
units, null/omitted measurements and escaped metadata independently of key order.
The suite also checks platform numeric-format compatibility, bounded output,
allocation/copy failures, clearing partial output, and that no failed
serialization reaches a transport or the buffer.
Host-only String reservation probes check one final payload construction per
publication and no reserialization during existing-buffer replay.

Sensor regressions compile the production BME680 wrapper against a host fake.
They check that the firmware's default correction converts a raw 22.1 C to
19.0 C, does not compound on repeated reads, and leaves other measurements
unchanged. This does not replace comparison with a calibrated thermometer on
the device under stable conditions after deployment.

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

For #11, isolate each external service failure rather than only stopping the
combined Java application:

- **HTTP only:** configure only HTTP publishing. Test both refused connections
  and an endpoint accepting TCP but never returning response headers.
- **MQTT only:** disable HTTP publishing and stop/restart the broker.
- **Combined:** stop each service separately, then both together. A success
  through either enabled transport is sufficient; MQTT QoS 0 retains its
  existing best-effort semantics.

For each mode, test with a nonempty buffer. Probe `/status/json` every second
for at least 60 seconds during the outage; require every response within one
second on the same LAN. Record sample timestamps/counts and verify their
configured cadence, stable uptime, buttons/display responsiveness and no
watchdog resets. Restore the service and confirm retained records and current
samples arrive without rebooting; HTTP-only replay must not drain on a failed
or incomplete HTTP response. Serial logs should show bounded failure/retry
and replay-progress messages rather than a full-backlog retry burst.

Also check the listener topic (if configured); incoming payload chunks must
print intact, and the listener is subscribed after each clean connection.

QoS remains 0: async queue acceptance is not a broker delivery acknowledgment.
Physical WiFi conditions, real broker compatibility and sustained operation
must be checked on the device, not inferred from these host fakes.
