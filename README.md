# ESP8266 BME680 Display v2

A modular rewrite of the recovered ESP8266/BME680/OLED application.

## Target
- ESP8266 / NodeMCU
- BME680 on I2C
- 128x32 SSD1306 OLED
- Adafruit sensor/display libraries
- Arduino ESP8266 core

## Preserved external behaviour
The original HTTP endpoints remain available:
- `/`
- `/sensor/read`
- `/status`
- `/settings`
- `/aplist`
- `/nodemcu.css`
- `/nodemcu.js`
- `/status/json`

The legacy configuration format at EEPROM offset 0/2 is detected and migrated to the v2 configuration format.

## Important changes
- No `exit(1)` from application code.
- No dynamic linked-list allocation for display windows.
- Measurement storage is a real ring buffer with a valid sample count.
- `millis()` scheduling is rollover-safe.
- HTTP requests have timeouts.
- Failed push measurements are buffered in SPIFFS and retried.
- Battery percentage is calculated consistently using 2.78..3.78 V as the legacy intended range.
- The legacy BME680 temperature correction of -4.0 C is retained.
- Gas resistance is named `gasResistance`; the old `VOC` label was a naming mistake.
- Logger variadic formatting is fixed.

## PlatformIO CLI
Run `pio run` from the repository root to build the NodeMCU firmware.
The output is `.pio/build/nodemcuv2/firmware.bin`.
`src/main.cpp` includes the root sketch for PlatformIO, bypassing automatic
prototype generation that misinterprets the embedded JavaScript.

## GitHub quality gates

Pull requests and pushes to `main` run `.github/workflows/quality.yml`:

- **ESP8266 build**: compile the real NodeMCU firmware with pinned PlatformIO,
  platform and direct library versions; retain `.bin`/`.elf` artifacts for 7 days.
- **C++ formatting**: clang-format 18.1.8 checks all project C++/Arduino sources
  and host test fakes against `.clang-format`. CI never rewrites or commits files.
- **Host tests and coverage**: compile with warnings as errors, run AddressSanitizer
  and UndefinedBehaviorSanitizer, then enforce at least **80% line and 80% branch
  coverage of production publishing code**. HTML, JSON and Cobertura reports are
  retained for 7 days, including when a coverage threshold fails.

Coverage does not include host fakes or unrelated hardware code. Compiler-generated
unreachable/exception-only branches are excluded; normal application branches are
counted. Passing CI is not proof of physical WiFi, broker recovery, sensors or
display behavior. Continue to use the device smoke test for those checks.

Run the same gates locally (Linux, Python 3.9+ and a `g++`/`gcov` toolchain):

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r tools/requirements-ci.txt
python tools/format.py --check
bash tests/host/run.sh --sanitize
bash tests/host/run.sh --coverage
pio run --environment nodemcuv2
```

Use `python tools/format.py --fix` to apply formatting. Coverage reports are written
to `build/coverage/`; build products, `.venv` and external dependencies are not
formatting inputs.

To make these checks block merges, after the first workflow run configure a
GitHub branch ruleset for `main` requiring **ESP8266 build**, **C++ formatting** and
**Host tests and coverage**. The workflow alone does not enable branch protection.
Checks run for all PR changes (no path filters), so required checks are not left
pending when only documentation changes.

## Measurement JSON contract

`MeasurementJsonSerializer` owns measurement field names, nesting, precision,
units and the existing station version field. It receives only the measurement
and device metadata/field-selection references, not network clients or the whole
application configuration. The sketch obtains/normalizes the hardware MAC once
during WiFi setup. `Publisher` only coordinates transport delivery.

Each new publication is serialized once; the same payload is passed to HTTP,
MQTT or buffering. Existing buffered JSON is replayed without reserialization.
`/sensor/read` uses the same serializer. JSON field names, nesting, unit labels,
version and all unrelated values remain unchanged.

**Gas-resistance value correction (#14):** `measurement.gasResistance.value`
now contains kilo-ohms, matching its existing `kOhm` unit. For example,
`261728` ohms serializes as `261.7` kilo-ohms at the existing one-decimal
precision. Older firmware incorrectly published the raw ohm value with the same
`kOhm` label. The correction is applied only in the serializer, so HTTP, MQTT
and `/sensor/read` share the same value semantics. Internal measurements remain
in ohms; the separate `/status/json` raw reading is unchanged.

Consumers must use the corrected value directly as kilo-ohms; remove any
workaround that divided the old published value by 1000. Historical data and
existing buffered JSON are **not rewritten**: replay preserves their original
values. Drain the old backlog before deploying if old/new semantics must not
mix, and record the deployment cutover for historical-data handling. The
unchanged field/unit/version alone cannot distinguish an old buffered record.

Serialization uses a bounded 1024-byte scratch payload and checks allocation
and final output-copy success. Finite numeric formatting retains Arduino String's
existing precision/rounding, with one small numeric temporary at a time.
Non-finite required values, oversized metadata or allocation failure clear the
output and prevent publishing/buffering a partial record; `/sensor/read` returns
HTTP 503 on serialization failure. Disabled measurement values may be non-finite
because they are omitted. JSON string controls are escaped.

The host suite parses representative output using Python's standard JSON parser,
checks the device identity, timestamp, values and units, and verifies optional
fields plus failure/no-partial-publication behavior. No new JSON dependency or
persisted format is introduced.

## MQTT recovery

MQTT uses `AsyncMqttClient` 0.9.0 and the ESP8266-specific `ESPAsyncTCP` 2.0.0.
DNS, TCP connection and MQTT handshaking are asynchronous; `MqttPublisher`
advances reconnect state once per main-loop iteration, without retry loops or
delays. Failed attempts back off for 5, 10, 20, 40, 80, 160 and at most 300
seconds; a successful connection resets the delay. Pending attempts time out
after 30 seconds, and attempts wait until WiFi is connected.

Publishing fails immediately when disconnected, allowing the SPIFFS buffer
path to retain the measurement. Buffered replay advances from the main loop.
Either enabled transport succeeding remains sufficient;
a disabled transport no longer masks a failure of the other one.

Payloads are copied with their explicit length by the asynchronous client.
QoS remains 0 and success means queued, not broker-acknowledged delivery;
messages accepted just before a disconnect can still be lost. No durable
acknowledgment queue, new buffer format or TLS configuration is introduced.
Heap headroom is reserved during buffered replay. Connection transitions are
logged at info level, and failures/next retry delays at warning level.
Info logging is enabled by default. MQTT logs include connection-attempt numbers
(since boot, including the initial attempt), back-off and uptime in milliseconds.
A status summary every 30 seconds, and on WiFi status changes, shows connection
state, WiFi readiness, attempts, active back-off and remaining retry wait.
An expired retry can still wait for WiFi; `retry_in_ms=0` does not override that
gate. No credentials are included.

Run `bash tests/host/run.sh` for focused host regressions; see
`tests/host/README.md` for the device outage/recovery smoke check.

## HTTP publishing and buffered replay

`HttpPublisher` reuses `ESPAsyncTCP` for asynchronous DNS, connection and POST
transmission. The supported transport is plain `http://` (no TLS changes).
Explicit ports, paths/query strings, Basic authentication and complete
length-delimited JSON payloads are retained. Success requires complete HTTP
2xx response headers after the request has been queued for transmission; queue
acceptance alone never acknowledges an HTTP-only record.

There is one HTTP request in flight, a five-second total request deadline, and
a five-second cooldown after failure. Sending is bounded to 512 bytes per
update, with no network-wait loops. Response parsing stores only a bounded
status line and ignores the response body; oversized/malformed headers fail
without logging credentials or response content.

`BufferedPublisher` coordinates the existing `buffer.json` newline-delimited
records. Live requests are tried without a flash write; new samples arriving
while a request/replay is pending are appended, and a failed live request is
appended after completion. Replay reads at most 128 bytes per step, at least
10 ms apart, and dispatches at most one record per step. Failures pause replay
for five seconds instead of scanning/retrying the full backlog.

The original buffer is retained until every record has succeeded through an
enabled transport. An acknowledged replay offset is kept only in RAM: a reboot
can resend a delivered prefix, but does not depend on deleting/replacing a
partially replayed file. JSON, EEPROM and buffer formats are unchanged.
The existing approximately 2 MB capacity remains bounded; full/unavailable
storage is logged and may reject new records. Records over 4096 bytes are
retained with a diagnostic rather than exhausting RAM (normal serializer output
fits below this guard). This is not an exactly-once queue, and a live HTTP
request in RAM can still be lost on power failure, as before.

Failure/retry logs and rate-limited replay-progress logs are available on the
serial port. Host coverage includes timeout, backpressure, segmented responses,
record retention and incremental replay. Physical responsiveness, sampling and
recovery still require the device outage test.

## Arduino IDE
Open `esp8266_bme680_display_v2.ino` after copying the `src` files into the sketch folder, or use PlatformIO directly.

## Compatibility note
The old source actually used the extended `esp8266_ap(1).h` configuration, including Domoticz fields and the NodeMCU/FeatherWing button pin mapping. v2 incorporates those settings directly rather than depending on that large generated header.
