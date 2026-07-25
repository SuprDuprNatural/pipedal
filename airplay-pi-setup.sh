#!/bin/bash
# One-shot Pi-side build + install of the PiPedal AirPlay receiver feature.
#
# Prerequisites (already done, or re-do after a Pi reboot since /tmp is tmpfs):
#   * a clone of rerdavies/pipedal at v2.0.108 in ~/src/pipedal
#       git clone --branch v2.0.108 --depth 1 --recurse-submodules \
#           --shallow-submodules https://github.com/rerdavies/pipedal.git ~/src/pipedal
#   * the airplay source patch at /tmp/airplay-src.patch. From your workstation,
#     with PI set to your own account and host (e.g. pi@raspberrypi.local):
#       cd path/to/pipedal && git diff v2.0.108 -- src > /tmp/airplay-src.patch
#       scp /tmp/airplay-src.patch "$PI":/tmp/
#     (works from the airplay working tree whether or not the changes are committed;
#      rack-view never touched src/, so this diff is exactly the airplay feature)
#   * (optional) the freshly built web UI:
#       scp -r path/to/pipedal/vite/dist "$PI":/tmp/react-dist
#
# Then run this script ON the Pi:
#   bash airplay-pi-setup.sh
#
# Revert to the stock apt binaries at any time with:
#   sudo cp /usr/sbin/pipedald.orig-2.0.108 /usr/sbin/pipedald
#   sudo cp /usr/sbin/pipedaladmind.orig-2.0.108 /usr/sbin/pipedaladmind
#   sudo systemctl restart pipedaladmind pipedald

set -e

SRC=~/src/pipedal

# ---- 0. apply the airplay patch if not applied yet -------------------------
cd "$SRC"
if [ ! -f src/AirplayInputStream.cpp ]; then
    if [ ! -f /tmp/airplay-src.patch ] && [ -f /tmp/airplay.b64 ]; then
        base64 -d /tmp/airplay.b64 > /tmp/airplay-src.patch
    fi
    if [ ! -f /tmp/airplay-src.patch ]; then
        echo "ERROR: /tmp/airplay-src.patch not found. Copy it from the Mac first (see header)."
        exit 1
    fi
    git apply /tmp/airplay-src.patch
    echo "airplay patch applied."
fi

# ---- 1. build dependencies -------------------------------------------------
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
    cmake ninja-build build-essential g++ \
    liblilv-dev libboost-dev libsystemd-dev libasound2-dev uuid-dev \
    authbind libavahi-client-dev libnm-dev libicu-dev \
    libsdbus-c++-dev libzip-dev google-perftools libgoogle-perftools-dev \
    libpipewire-0.3-dev libbz2-dev libssl-dev librsvg2-dev

# ---- 2. the AirPlay receiver itself ----------------------------------------
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y shairport-sync
# PiPedal manages its own shairport-sync instance (pipedal-airplay.service);
# the stock service would advertise a second, broken AirPlay endpoint.
sudo systemctl disable --now shairport-sync.service 2>/dev/null || true

# ---- 3. build pipedald + pipedaladmind (no npm needed for these targets) ---
cd "$SRC"
mkdir -p build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja -j3 pipedald pipedaladmind

# ---- 4. install binaries (backing up the stock apt ones first) -------------
sudo systemctl stop pipedald pipedaladmind
for b in pipedald pipedaladmind; do
    if [ ! -f /usr/sbin/$b.orig-2.0.108 ]; then
        sudo cp /usr/sbin/$b /usr/sbin/$b.orig-2.0.108
    fi
    sudo cp src/$b /usr/sbin/$b
done

# ---- 5. web UI (from a dist directory, tarball, or transferred chunks) ------
if [ -d /tmp/rchunks ] && [ ! -f /tmp/react-dist.tgz ]; then
    cat /tmp/rchunks/rchunk_* > /tmp/react.b64
    base64 -d /tmp/react.b64 > /tmp/react-dist.tgz
fi
if [ -f /tmp/react-dist.tgz ]; then
    sudo rm -rf /etc/pipedal/react/*
    sudo tar xzf /tmp/react-dist.tgz -C /etc/pipedal/react
    echo "web UI deployed."
elif [ -d /tmp/react-dist ]; then
    sudo rm -rf /etc/pipedal/react/*
    sudo cp -r /tmp/react-dist/* /etc/pipedal/react/
    echo "web UI deployed."
fi

sudo systemctl start pipedaladmind
sudo systemctl start pipedald

echo
echo "Done. Toggle AirPlay on from the switch in the PiPedal toolbar."
echo "The first enable writes /etc/pipedal/shairport-sync.conf and"
echo "pipedal-airplay.service, then starts it. AirPlay is always off after a"
echo "pipedald restart; see docs/AirPlay.md."
