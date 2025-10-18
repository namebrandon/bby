#!/bin/bash
# Quick build script (Linux native)
BUILD_TYPE=${1:-Release}
exec /workspace/build-linux.sh $BUILD_TYPE
