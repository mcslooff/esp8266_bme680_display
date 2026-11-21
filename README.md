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

## MQTT recovery

MQTT uses `AsyncMqttClient` 0.9.0 and the ESP8266-specific `ESPAsyncTCP` 2.0.0.
DNS, TCP connection and MQTT handshaking are asynchronous; `MqttPublisher`
advances reconnect state once per main-loop iteration, without retry loops or
delays. Failed attempts back off for 5, 10, 20, 40, 80, 160 and at most 300
seconds; a successful connection resets the delay. Pending attempts time out
after 30 seconds, and attempts wait until WiFi is connected.

Publishing fails immediately when disconnected, allowing the existing SPIFFS
buffer path to retain the measurement. Buffered replay runs during the normal
publication cycle. Either enabled transport succeeding remains sufficient;
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

## Arduino IDE
Open `esp8266_bme680_display_v2.ino` after copying the `src` files into the sketch folder, or use PlatformIO directly.

## Compatibility note
The old source actually used the extended `esp8266_ap(1).h` configuration, including Domoticz fields and the NodeMCU/FeatherWing button pin mapping. v2 incorporates those settings directly rather than depending on that large generated header.
