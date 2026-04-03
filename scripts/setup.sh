#!/bin/bash
# setup.sh — initialize mod-pi-touch project dependencies
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR/.."

echo "=== Installing system dependencies ==="
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake pkg-config git \
    liblilv-dev libserd-dev libsord-dev \
    libjack-jackd2-dev \
    libevdev-dev

echo "=== Initializing git submodules ==="
cd "$ROOT"
git init 2>/dev/null || true
git submodule update --init --recursive

echo "=== Build ==="
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

echo ""
echo "=== Done! Binary at: $ROOT/build/mod-pi-touch ==="
echo ""
echo "Usage:"
echo "  ./build/mod-pi-touch"
echo ""
echo "Environment variables:"
echo "  MPT_HOST_ADDR=127.0.0.1  (mod-host IP)"
echo "  MPT_HOST_CMD_PORT=5555"
echo "  MPT_HOST_FB_PORT=5556"
echo "  MPT_FB_DEVICE=/dev/fb0"
echo "  MPT_TOUCH_DEVICE=/dev/input/event0"
echo "  MPT_PEDALBOARDS=~/.pedalboards"
echo "  LV2_PATH=~/.lv2"
