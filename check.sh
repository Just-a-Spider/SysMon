#!/usr/bin/env bash
set -e

echo "=== Checking sysmon-server ==="
cd sysmon-server
cargo check
cargo test
cd ..

if [ -n "$DEVKITARM" ] || command -v arm-none-eabi-gcc >/dev/null 2>&1; then
    echo "=== Checking sysmon-3ds ==="
    make -C sysmon-3ds
fi

echo "=== All local checks passed successfully ==="
