#!/bin/sh
# Runs the end-to-end GUI workflow test. Usage: tests/run-smoke.sh BUILD_DIR
set -eu
exec python3 "$(dirname "$0")/smoke.py" "${1:-build}"
