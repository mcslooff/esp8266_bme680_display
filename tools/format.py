#!/usr/bin/env python3
"""Check or fix the project's C++/Arduino files, excluding dependencies."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="Fail on formatting differences")
    mode.add_argument("--fix", action="store_true", help="Apply the repository style")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    formatter = shutil.which("clang-format")
    if not formatter:
        parser.error("Install tools/requirements-ci.txt to get clang-format 18.1.8")
    version = subprocess.check_output([formatter, "--version"], text=True)
    if not re.search(r"\bversion 18\.1\.8\b", version):
        parser.error("clang-format 18.1.8 is required; found " + version.strip())

    # Include new files locally as well as tracked ones, but never scan .pio,
    # local configuration, external packages or generated coverage reports.
    names = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=root,
    ).decode().split("\0")
    files = sorted({
        name for name in names
        if name
        and Path(name).suffix in {".cpp", ".h", ".hpp", ".ino"}
        and (Path(name).parts[0] in {"src", "tests"} or name == "esp8266_bme680_display.ino")
        and (root / name).is_file()
    })
    if not files:
        parser.error("No project source files found")
    options = ["--dry-run", "--Werror"] if args.check else ["-i"]
    print(f"{'Checking' if args.check else 'Formatting'} {len(files)} source files", flush=True)
    return subprocess.run(
        [formatter, "--style=file", "--fallback-style=none", *options, *files],
        cwd=root,
    ).returncode


if __name__ == "__main__":
    sys.exit(main())
