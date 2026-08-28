#!/bin/sh
set -eu

if [ "${DYLD_INSERT_LIBRARIES+x}" = x ]; then
    echo "DYLD_INSERT_LIBRARIES leaked into arch" 1>&2
    exit 1
fi
if [ "${DYLD_LIBRARY_PATH+x}" = x ]; then
    echo "DYLD_LIBRARY_PATH leaked into arch" 1>&2
    exit 1
fi
if [ "${ARCHPREFERENCE-}" != "${EXPECTED_ARCHPREFERENCE}" ]; then
    echo "arch received unexpected architecture preference: ${ARCHPREFERENCE-}" 1>&2
    exit 1
fi

[ "$1" = "-e" ] || exit 2
case "$2" in
    DYLD_LIBRARY_PATH=*) library_path=${2#DYLD_LIBRARY_PATH=} ;;
    *) exit 2 ;;
esac

[ "$3" = "-e" ] || exit 2
case "$4" in
    DYLD_INSERT_LIBRARIES=*) inserted=${4#DYLD_INSERT_LIBRARIES=} ;;
    *) exit 2 ;;
esac

if [ "${library_path}" != "${EXPECTED_DYLD_LIBRARY_PATH}" ]; then
    echo "arch received unexpected library path: ${library_path}" 1>&2
    exit 1
fi

[ "${inserted}" = "${EXPECTED_DYLD_INSERT_LIBRARIES}" ] || {
    echo "arch received unexpected inserted libraries: ${inserted}" 1>&2
    exit 1
}

printf '%s\n' 'arch-ok'
