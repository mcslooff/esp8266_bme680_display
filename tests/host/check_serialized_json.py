"""Verify emitted measurement JSON using a real parser, not field order."""

import json
from pathlib import Path
import sys


def invalid_number(value):
    raise ValueError(f"Invalid JSON number: {value}")


def check(path):
    fixtures = {}
    for line in Path(path).read_text().splitlines():
        if line.startswith("SERIALIZER_"):
            name, payload = line.split(" ", 1)
            fixtures[name] = json.loads(payload, parse_constant=invalid_number)
    assert set(fixtures) == {"SERIALIZER_CONTRACT", "SERIALIZER_DISABLED", "SERIALIZER_SELECTED"}
    data = fixtures["SERIALIZER_CONTRACT"]
    assert set(data) == {"timestamp", "station", "measurement"}
    assert data["timestamp"] == 123456
    assert data["station"] == {
        "id": "123456789ABC",
        "name": 'Room "A"\\\n\r\t\001',
        "version": "2.0.0",
        "power": {"voltage": 3.5},
    }
    assert data["measurement"] == {
        "temperature": {"value": 21.5, "unit": "C"},
        "airPressure": {"value": 1013.25, "unit": "hPa"},
        "humidity": {"value": 50.0, "unit": "%"},
        # 261728 ohms is 261.728 kilo-ohms, at the existing one-decimal precision.
        "gasResistance": {"value": 261.7, "unit": "kOhm"},
    }
    assert fixtures["SERIALIZER_DISABLED"]["measurement"] is None
    assert fixtures["SERIALIZER_SELECTED"]["measurement"] == {
        "humidity": {"value": 50.0, "unit": "%"}
    }
    print("Parsed measurement JSON contract passed")


if __name__ == "__main__":
    check(sys.argv[1])
