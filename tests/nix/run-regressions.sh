#!/bin/sh
set -eu

if [ "$#" -ne 3 ] || [ ! -f "$1" ] || [ ! -f "$2" ] || [ ! -x "$3" ]; then
    echo "Usage: $0 /path/to/libdoorstop.so /path/to/UnityPlayer.so /path/to/unityplayer-dup2-smoke" 1>&2
    exit 2
fi

a="/$0"; a=${a%/*}; a=${a#/}; a=${a:-.}; TEST_DIR=$(cd "$a" || exit; pwd -P)
REPO_DIR=$(cd "${TEST_DIR}/../.." || exit; pwd -P)
LIBDOORSTOP=$(cd "$(dirname "$1")" || exit; pwd -P)/$(basename "$1")
UNITYPLAYER_LIB=$(cd "$(dirname "$2")" || exit; pwd -P)/$(basename "$2")
UNITYPLAYER_SMOKE=$(cd "$(dirname "$3")" || exit; pwd -P)/$(basename "$3")
TMP_ROOT=${TMPDIR:-/tmp}
TEST_TMP="${TMP_ROOT%/}/unity-doorstop-tests-$$"

cleanup() {
    rm -rf "${TEST_TMP}"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "${TEST_TMP}"

fail() {
    echo "FAIL: $*" 1>&2
    exit 1
}

# Issue #108 is macOS-only. Linux CI keeps lightweight source invariants while
# the macOS job runs the real process-level dlsym interposition smoke test.
entrypoint_source="${REPO_DIR}/src/nix/entrypoint.c"
plthook_osx_source="${REPO_DIR}/src/nix/plthook/plthook_osx.c"
grep -q 'DYLD_INTERPOSE(dlsym_hook, dlsym)' "${entrypoint_source}" ||
    fail "macOS dlsym interposition is missing"
grep -q 'DYLD_INTERPOSE(fopen_hook, fopen)' "${entrypoint_source}" ||
    fail "macOS fopen interposition is missing"
grep -q 'DYLD_INTERPOSE(fclose_hook, fclose)' "${entrypoint_source}" ||
    fail "macOS fclose interposition is missing"
grep -q 'DYLD_INTERPOSE(dup2_hook, dup2)' "${entrypoint_source}" ||
    fail "macOS dup2 interposition is missing"
grep -q '!doorstop_ready || !config.enabled' "${entrypoint_source}" ||
    fail "macOS dlsym interposition guard is missing"
grep -q 'APPLE_CALLER_IS_UNITY_PLAYER' "${entrypoint_source}" ||
    fail "macOS stdio interposition caller guard is missing"
grep -q 'linkedit->vmaddr - linkedit->fileoff + d->slide' \
    "${plthook_osx_source}" ||
    fail "chained-fixups header is not relative to __LINKEDIT"
grep -q 'chained-fixup PLT enumeration is unsupported' \
    "${plthook_osx_source}" ||
    fail "unsafe chained-fixups enumeration is not fail-closed"

# Issue #88: a preloaded Doorstop must not replace sh's own dup2 PLT entry.
shell_output=$(
    DOORSTOP_ENABLED=1 \
    DOORSTOP_TARGET_ASSEMBLY=/dev/null \
    LD_PRELOAD="${LIBDOORSTOP}" \
    /bin/sh -c 'value="$(printf captured)"; printf "[%s]" "$value"'
)
[ "${shell_output}" = "[captured]" ] ||
    fail "command substitution was not captured: ${shell_output}"

# Issue #47 is the same leak through ordinary file redirection: child tools
# must still be able to write generated content to a file without it escaping
# to the launcher's stdout.
redirected_file="${TEST_TMP}/redirected-output"
redirected_stdout=$(
    DOORSTOP_ENABLED=1 \
    DOORSTOP_TARGET_ASSEMBLY=/dev/null \
    LD_PRELOAD="${LIBDOORSTOP}" \
    /bin/sh -c 'printf redirected > "$1"' sh "${redirected_file}"
)
[ -z "${redirected_stdout}" ] ||
    fail "file redirection leaked to stdout: ${redirected_stdout}"
[ "$(cat "${redirected_file}")" = "redirected" ] ||
    fail "file redirection did not reach its destination"

# Issues #34/#73: an opted-in replacement process must not inherit the two
# markers that belong to its launcher process.
marker_output=$(
    DOORSTOP_ENABLED=1 \
    DOORSTOP_IGNORE_DISABLED_ENV=1 \
    DOORSTOP_INITIALIZED=TRUE \
    DOORSTOP_DISABLE=TRUE \
    LD_PRELOAD="${LIBDOORSTOP}" \
    /bin/sh -c \
        'printf "[%s][%s]" "${DOORSTOP_INITIALIZED-}" "${DOORSTOP_DISABLE-}"'
)
[ "${marker_output}" = "[][]" ] ||
    fail "inherited Doorstop markers were not cleared: ${marker_output}"

# Guard the original reason for the dup2 hook as well: calls originating in a
# real UnityPlayer module still cannot redirect stdout away from the console.
unityplayer_output=$(
    DOORSTOP_ENABLED=1 \
    DOORSTOP_TARGET_ASSEMBLY=/dev/null \
    LD_LIBRARY_PATH="$(dirname "${UNITYPLAYER_LIB}"):${LD_LIBRARY_PATH-}" \
    LD_PRELOAD="${LIBDOORSTOP}" \
    "${UNITYPLAYER_SMOKE}"
)
[ "${unityplayer_output}" = "visibleunityplayer-dup2-ok" ] ||
    fail "UnityPlayer stdout protection regressed: ${unityplayer_output}"

linux_case="${TEST_TMP}/linux-case"
mkdir -p "${linux_case}"
cp "${REPO_DIR}/assets/nix/run.sh" "${linux_case}/run.sh"
cp "${LIBDOORSTOP}" "${linux_case}/libdoorstop.so"
cp "${TEST_DIR}/fixtures/game.sh" "${linux_case}/fixture-game"
chmod +x "${linux_case}/fixture-game"

# Issue #88: now that inherited injection no longer breaks helper processes,
# SteamLaunch arguments must flow directly to the selected executable instead
# of triggering the old delayed-injection re-exec workaround.
steam_output=$(
    cd "${linux_case}" &&
    /bin/sh ./run.sh ./fixture-game SteamLaunch
)
[ "${steam_output}" = "game-ok" ] ||
    fail "SteamLaunch still triggered delayed injection: ${steam_output}"

# Debug-only mode represents the absence of a managed entrypoint with an empty
# target value. Do not turn it into BASEDIR, which is an existing directory and
# would be mistaken for an assembly by access(F_OK).
empty_target_output=$(
    cd "${linux_case}" &&
    /bin/sh ./run.sh ./fixture-game \
        --doorstop-target-assembly "" \
        --print-target-assembly
)
[ "${empty_target_output}" = "[]" ] ||
    fail "empty target assembly was rewritten: ${empty_target_output}"

# Issue #67: each non-empty Mono search path is relative to run.sh.
# Multiple entries remain colon-separated.
mkdir -p "${linux_case}/search-one" "${linux_case}/search two"
search_output=$(
    cd "${linux_case}" &&
    /bin/sh ./run.sh ./fixture-game \
        --doorstop-mono-dll-search-path-override \
        "search-one::search two:" \
        --print-search-path
)
expected_search_path="${linux_case}/search-one:${linux_case}/search two"
[ "${search_output}" = "${expected_search_path}" ] ||
    fail "Mono search paths were not resolved from run.sh: ${search_output}"

# Issue #107: simulate Apple Silicon and assert that the arch helper starts
# without DYLD_INSERT_LIBRARIES, while arch -e receives the complete value for
# the game.
mac_case="${TEST_TMP}/mac-case"
fake_bin="${mac_case}/fake-bin"
game_app="${mac_case}/TestGame.app/Contents/MacOS"
mkdir -p "${fake_bin}" "${game_app}"
cp "${REPO_DIR}/assets/nix/run.sh" "${mac_case}/run.sh"
cp "${LIBDOORSTOP}" "${mac_case}/libdoorstop.dylib"
cp "${TEST_DIR}/fixtures/game.sh" "${game_app}/GameBin"
cp "${TEST_DIR}/fixtures/fake-uname.sh" "${fake_bin}/uname"
cp "${TEST_DIR}/fixtures/fake-sysctl.sh" "${fake_bin}/sysctl"
cp "${TEST_DIR}/fixtures/fake-defaults.sh" "${fake_bin}/defaults"
cp "${TEST_DIR}/fixtures/fake-file.sh" "${fake_bin}/file"
cp "${TEST_DIR}/fixtures/fake-arch.sh" "${fake_bin}/arch"
chmod +x "${game_app}/GameBin" "${fake_bin}/uname" "${fake_bin}/sysctl" \
    "${fake_bin}/defaults" "${fake_bin}/file" "${fake_bin}/arch"

mac_output=$(
    cd "${mac_case}" &&
    PATH="${fake_bin}:${PATH}" \
    DYLD_LIBRARY_PATH=existing/lib \
    DYLD_INSERT_LIBRARIES=existing.dylib \
    EXPECTED_ARCHPREFERENCE=x86_64 \
    EXPECTED_DYLD_LIBRARY_PATH="${mac_case}/:existing/lib" \
    EXPECTED_DYLD_INSERT_LIBRARIES=libdoorstop.dylib:existing.dylib \
    /bin/sh ./run.sh TestGame.app --doorstop-macos-architectures x86_64
)
[ "${mac_output}" = "arch-ok" ] ||
    fail "Apple Silicon arch environment was not isolated: ${mac_output}"

echo "nix regression tests passed"
