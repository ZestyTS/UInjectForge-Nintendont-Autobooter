#!/bin/sh
set -eu
export PATH="$DEVKITPPC/bin:$PATH"
make -j2
make -j2 DEBUG_BUILD=1
