#!/bin/sh
set -eu

if [ "${DYLD_INSERT_LIBRARIES+x}" = x ]; then
    echo "DYLD_INSERT_LIBRARIES leaked into arch" 1>&2
    exit 1
fi

[ "$1" = "-e" ] || exit 2
case "$2" in
    DYLD_INSERT_LIBRARIES=*) inserted=${2#DYLD_INSERT_LIBRARIES=} ;;
    *) exit 2 ;;
esac

[ "${inserted}" = "${EXPECTED_DYLD_INSERT_LIBRARIES}" ] || {
    echo "arch received unexpected inserted libraries: ${inserted}" 1>&2
    exit 1
}

printf '%s\n' 'arch-ok'
