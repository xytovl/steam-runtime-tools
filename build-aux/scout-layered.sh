#!/bin/sh
# Copyright 2021-2023 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

builddir="${1:-_build/scout-layered}"

rm -fr "$builddir/steam-container-runtime/depot"
install -d "$builddir/steam-container-runtime/depot"
install -d "$builddir/steam-container-runtime/steampipe"
install -m644 \
    subprojects/container-runtime/steampipe/app_build_1070560.vdf \
    subprojects/container-runtime/steampipe/depot_build_1070561.vdf \
    "$builddir/steam-container-runtime/steampipe/"

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
    --depot="$builddir/steam-container-runtime/depot" \
    --depot-archive="$builddir/SteamLinuxRuntime.tar.xz" \
    --depot-version="${depot_version#v}" \
    --layered \
    --steam-app-id=1070560 \
    --steam-depot-id=1070561 \
    scout
tar -tvf "$builddir/SteamLinuxRuntime.tar.xz"
head -n-0 "$builddir/steam-container-runtime/depot/VERSIONS.txt"
rm -fr "$builddir/steam-container-runtime/depot/steampipe"
rm -fr "$builddir/steam-container-runtime/depot/var"
tar \
    -C "$builddir" \
    --clamp-mtime \
    --mtime="@${SOURCE_DATE_EPOCH}" \
    --owner=nobody:65534 \
    --group=nogroup:65534 \
    --mode=u=rwX,go=rX \
    --use-compress-program='pigz --fast -c -n --rsyncable' \
    -cvf "$builddir/steam-container-runtime.tar.gz" \
    steam-container-runtime
tar -tvf "$builddir/steam-container-runtime.tar.gz"

# vim:set sw=4 sts=4 et:
