#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mode=${1:-normal}
if (( $# > 1 )); then
  echo "Usage: bash tests/host/run.sh [--sanitize|--coverage]" >&2
  exit 2
fi
flags=()
case "$mode" in
  normal) ;;
  --sanitize)
    flags=(-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all)
    ;;
  --coverage)
    command -v gcovr >/dev/null || {
      echo "Install tools/requirements-ci.txt to run coverage checks" >&2
      exit 2
    }
    flags=(-g -O0 --coverage)
    ;;
  *)
    echo "Usage: bash tests/host/run.sh [--sanitize|--coverage]" >&2
    exit 2
    ;;
esac
workdir=$(mktemp -d "${TMPDIR:-/tmp}/publishing-tests.XXXXXX")
trap 'rm -rf "$workdir"' EXIT
binary="$workdir/publishing-test"
"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror \
  "${flags[@]}" \
  -I tests/host/stubs \
  tests/host/publishing_test.cpp \
  src/publishing/Publisher.cpp src/publishing/MqttPublisher.cpp \
  src/system/Logger.cpp src/storage/MeasurementStore.cpp \
  -o "$binary"
if [[ "$mode" == --sanitize ]]; then
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$binary"
else
  "$binary"
fi
if [[ "$mode" == --coverage ]]; then
  mkdir -p build/coverage
  gcovr --config gcovr.cfg --root . "$workdir" --print-summary \
    --xml-pretty --xml build/coverage/cobertura.xml \
    --html-details build/coverage/index.html \
    --json build/coverage/coverage.json \
    --json-summary build/coverage/summary.json
fi
