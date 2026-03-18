#!/bin/bash
set -e

cd spatial_engine/realtimeEngine/
rm -rf build/
mkdir build
cd build/
cmake ..
make -j"$(sysctl -n hw.ncpu)"

echo "Realtime Engine rebuilt"