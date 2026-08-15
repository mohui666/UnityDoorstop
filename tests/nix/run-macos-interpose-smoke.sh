#!/bin/sh
set -eu

if [ "$(uname -s)" != "Darwin" ] || [ "$#" -ne 1 ] || [ ! -f "$1" ]; then
    echo "Usage on macOS: $0 /path/to/libdoorstop.dylib" 1>&2
    exit 2
fi

a="/$0"; a=${a%/*}; a=${a#/}; a=${a:-.}; TEST_DIR=$(cd "$a" || exit; pwd -P)
LIBDOORSTOP=$(cd "$(dirname "$1")" || exit; pwd -P)/$(basename "$1")
SMOKE_TMP=$(mktemp -d "${TMPDIR:-/tmp}/unity-doorstop-interpose.XXXXXX")

cleanup() {
    rm -rf "${SMOKE_TMP}"
}
trap cleanup EXIT HUP INT TERM

cc -Wall -Wextra -Werror -dynamiclib \
    -Wl,-install_name,@rpath/UnityPlayer.dylib \
    "${TEST_DIR}/fixtures/macos-unityplayer-interpose.c" \
    -o "${SMOKE_TMP}/UnityPlayer.dylib"

cc -Wall -Wextra -Werror -Wl,-export_dynamic \
    "${TEST_DIR}/fixtures/macos-dlsym-smoke.c" \
    -L"${SMOKE_TMP}" -lUnityPlayer -Wl,-rpath,@loader_path \
    -o "${SMOKE_TMP}/macos-dlsym-smoke"

unset DOORSTOP_DISABLE DOORSTOP_INITIALIZED DOORSTOP_TARGET_ASSEMBLY

DOORSTOP_ENABLED=0 \
DYLD_INSERT_LIBRARIES="${LIBDOORSTOP}" \
    "${SMOKE_TMP}/macos-dlsym-smoke" disabled

printf '%s\n' override > "${SMOKE_TMP}/override.config"

enabled_output=$(
    DOORSTOP_ENABLED=1 \
    DOORSTOP_BOOT_CONFIG_OVERRIDE="${SMOKE_TMP}/override.config" \
    EXPECTED_BOOT_CONFIG_PATH="$(pwd -P)/macos-dlsym-smoke_Data/boot.config" \
    REDIRECT_OUTPUT_PATH="${SMOKE_TMP}/redirected-output" \
    DYLD_INSERT_LIBRARIES="${LIBDOORSTOP}" \
        "${SMOKE_TMP}/macos-dlsym-smoke" enabled
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
