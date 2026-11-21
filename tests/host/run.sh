#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../.."
binary=$(mktemp "${TMPDIR:-/tmp}/publishing-test.XXXXXX")
trap 'rm -f "$binary"' EXIT
"${CXX:-g++}" -std=c++11 -Wall -Wextra -Werror \
  -I tests/host/stubs \
  tests/host/publishing_test.cpp \
  src/publishing/Publisher.cpp src/publishing/MqttPublisher.cpp \
  src/system/Logger.cpp src/storage/MeasurementStore.cpp \
  -o "$binary"
"$binary"
