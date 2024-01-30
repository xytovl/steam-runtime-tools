#!/bin/sh
# Copyright 2021-2024 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

builddir="${1:-_build/scout-layered}"

rm -fr "$builddir/SteamLinuxRuntime"
rm -fr "$builddir/steam-container-runtime"
rm -fr "$builddir/steam-container-runtime.tar.gz"
install -d "$builddir/SteamLinuxRuntime"

case "${CI_COMMIT_TAG-}" in
    (v*)
        depot_version="${CI_COMMIT_TAG}"
        ;;

    (*)
        depot_version="$(git describe --always --match="v*" HEAD)"
        ;;
esac

if [ -n "${SOURCE_DATE_EPOCH-}" ]; then
    :
elif [ -n "${CI_COMMIT_TIMESTAMP-}" ]; then
    SOURCE_DATE_EPOCH="$(date --date="${CI_COMMIT_TIMESTAMP}" '+%s')"
else
    SOURCE_DATE_EPOCH="$(git log -1 --pretty=format:'%at' HEAD)"
fi

export SOURCE_DATE_EPOCH

echo "${depot_version#v}" > subprojects/container-runtime/.tarball-version
./subprojects/container-runtime/populate-depot.py \
    --depot="$builddir/SteamLinuxRuntime" \
    --depot-archive="$builddir/SteamLinuxRuntime.tar.xz" \
    --depot-version="${depot_version#v}" \
    --layered \
    --steam-app-id=1070560 \
    --steam-depot-id=1070561 \
    scout
tar -tvf "$builddir/SteamLinuxRuntime.tar.xz"
head -n-0 "$builddir/SteamLinuxRuntime/VERSIONS.txt"

# vim:set sw=4 sts=4 et:
