#!/bin/sh
set -eu

if [ "$(uname -s)" != "Darwin" ] || [ "$#" -ne 3 ] || \
    [ ! -f "$1" ] || [ ! -f "$2" ] || [ ! -x "$3" ]; then
    echo "Usage on macOS: $0 /path/to/libdoorstop.dylib /path/to/UnityPlayer.dylib /path/to/macos-dlsym-smoke" 1>&2
    exit 2
fi

LIBDOORSTOP=$(cd "$(dirname "$1")" || exit; pwd -P)/$(basename "$1")
UNITYPLAYER_LIB=$(cd "$(dirname "$2")" || exit; pwd -P)/$(basename "$2")
SMOKE_BINARY=$(cd "$(dirname "$3")" || exit; pwd -P)/$(basename "$3")
# Resolve helper commands before enabling injection. macOS 26 system tools can
# be arm64e, while Doorstop deliberately ships arm64 and x86_64 slices.
UNITYPLAYER_DIR=$(dirname "${UNITYPLAYER_LIB}")
SMOKE_TMP=$(mktemp -d "${TMPDIR:-/tmp}/unity-doorstop-interpose.XXXXXX")

cleanup() {
    rm -rf "${SMOKE_TMP}"
}
trap cleanup EXIT HUP INT TERM

unset DOORSTOP_DISABLE DOORSTOP_INITIALIZED DOORSTOP_TARGET_ASSEMBLY

DOORSTOP_ENABLED=0 \
DYLD_INSERT_LIBRARIES="${LIBDOORSTOP}" \
DYLD_LIBRARY_PATH="${UNITYPLAYER_DIR}:${DYLD_LIBRARY_PATH-}" \
    "${SMOKE_BINARY}" disabled

printf '%s\n' override > "${SMOKE_TMP}/override.config"

enabled_output=$(
    DOORSTOP_ENABLED=1 \
    DOORSTOP_BOOT_CONFIG_OVERRIDE="${SMOKE_TMP}/override.config" \
    EXPECTED_BOOT_CONFIG_PATH="$(pwd -P)/macos-dlsym-smoke_Data/boot.config" \
    REDIRECT_OUTPUT_PATH="${SMOKE_TMP}/redirected-output" \
    DYLD_INSERT_LIBRARIES="${LIBDOORSTOP}" \
    DYLD_LIBRARY_PATH="${UNITYPLAYER_DIR}:${DYLD_LIBRARY_PATH-}" \
        "${SMOKE_BINARY}" enabled
)
[ "${enabled_output}" = "unity-interpose-ok" ] || {
    echo "macOS UnityPlayer interposition failed: ${enabled_output}" 1>&2
    exit 1
}
[ ! -s "${SMOKE_TMP}/redirected-output" ] || {
    echo "macOS UnityPlayer dup2 leaked output to the redirected file" 1>&2
    exit 1
}

echo "macOS dlsym interposition smoke test passed"
