#!/usr/bin/env bash
# Refresh the vendored third-party sources.  Run from the repository root.
set -euo pipefail
dst=firmware/components/sensirion_gas_index
base=https://raw.githubusercontent.com/Sensirion/gas-index-algorithm/master
curl -fsSL -o "$dst/sensirion_gas_index_algorithm.c" \
     "$base/sensirion_gas_index_algorithm/sensirion_gas_index_algorithm.c"
curl -fsSL -o "$dst/include/sensirion_gas_index_algorithm.h" \
     "$base/sensirion_gas_index_algorithm/sensirion_gas_index_algorithm.h"
curl -fsSL -o "$dst/LICENSE" "$base/LICENSE"
echo "vendored Sensirion gas-index-algorithm into $dst"
