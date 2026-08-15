#!/bin/sh
case "${1-}" in
    --print-search-path)
        printf '%s\n' "${DOORSTOP_MONO_DLL_SEARCH_PATH_OVERRIDE-}"
    ;;
    --print-target-assembly)
        printf '[%s]\n' "${DOORSTOP_TARGET_ASSEMBLY-}"
    ;;
    *)
        printf '%s\n' 'game-ok'
    ;;
esac
