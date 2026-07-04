#!/usr/bin/env bash
#
# Smoke-test a released native_sim FM artifact:
#   1. it is statically linked (ldd -> "not a dynamic executable")
#   2. `version` reports the expected YY.MM.DD-NN release version
#   3. `module fm describe` emits fm_transceiver with the expected band
#
# Usage: scripts/release_smoke.sh <native_sim_binary> <expected_version> <expected_band>
set -euo pipefail

bin=${1:?binary path required}
expected_ver=${2:?expected version required}     # e.g. 26.07.04-01
expected_band=${3:?expected band required}       # vhf | uhf

nl=$'\n'   # real newline; bash does not interpret \n inside "double quotes"
fail() { echo "::error::release_smoke: $*" >&2; exit 1; }

[ -x "$bin" ] || fail "$bin is not executable"

# 1. Static check.
ldd_out=$(LC_ALL=C ldd "$bin" 2>&1 || true)
printf '%s\n' "$ldd_out" | grep -q "not a dynamic executable" \
	|| fail "$bin is not statically linked: ${ldd_out}"

# 2+3. Drive the sim over stdio. timeout guards the EOF-hang; || true swallows
# the timeout's 124 so we can inspect whatever it printed.
out=$(printf 'version\nmodule fm describe\n' | timeout 15 "$bin" -uart_stdinout 2>&1 || true)

printf '%s\n' "$out" | grep -qF "APP-VERSION ${expected_ver}" \
	|| fail "version mismatch; expected APP-VERSION ${expected_ver}. Got:${nl}${out}"

# `|| true` keeps a no-match grep from tripping set -e/pipefail before the
# explicit guard below can emit the helpful fail message.
desc=$(printf '%s\n' "$out" | grep -o 'MODULE-DESCRIBE {.*}' | head -1 || true)
[ -n "$desc" ] || fail "no MODULE-DESCRIBE line. Got:${nl}${out}"
printf '%s\n' "$desc" | grep -q '"type":"fm_transceiver"' \
	|| fail "identity.type != fm_transceiver: ${desc}"
printf '%s\n' "$desc" | grep -q "\"version\":\"${expected_band}\"" \
	|| fail "identity.version != ${expected_band}: ${desc}"

echo "OK — ${bin}: static, version ${expected_ver}, band ${expected_band}"
