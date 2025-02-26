#!/bin/bash
# Copyright © 2021 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eu

if [ -n "${TESTS_ONLY-}" ]; then
    echo "1..0 # SKIP This distro is too old to run populate-depot.py"
    exit 0
fi

populate_depot_args=("--steam-app-id=1391110")

if [ -n "${IMAGES_DOWNLOAD_CREDENTIAL-}" ]; then
    populate_depot_args=( \
        "${populate_depot_args[@]}" \
        --credential-env IMAGES_DOWNLOAD_CREDENTIAL \
    )
fi

if [ -n "${IMAGES_DOWNLOAD_URL-}" ] && [ -n "${IMAGES_DOWNLOAD_CREDENTIAL-}" ]; then
    populate_depot_args=( \
        "${populate_depot_args[@]}" \
        --images-uri "${IMAGES_DOWNLOAD_URL}"/steamrt-SUITE/snapshots \
    )
elif [ -n "${IMAGES_SSH_HOST-}" ] && [ -n "${IMAGES_SSH_PATH-}" ]; then
    populate_depot_args=( \
        "${populate_depot_args[@]}" \
        --ssh-host "${IMAGES_SSH_HOST}" \
        --ssh-path "${IMAGES_SSH_PATH}" \
    )
else
    populate_depot_args=( \
        "${populate_depot_args[@]}" \
        --version latest-container-runtime-public-beta \
    )
fi

pressure_vessel_args=()

if [ -n "${PRESSURE_VESSEL_SSH_HOST-"${IMAGES_SSH_HOST-}"}" ] && [ -n "${PRESSURE_VESSEL_SSH_PATH-}" ]; then
    pressure_vessel_args=( \
        "${pressure_vessel_args[@]}" \
        --pressure-vessel-ssh-host="${PRESSURE_VESSEL_SSH_HOST-"${IMAGES_SSH_HOST}"}" \
        --pressure-vessel-ssh-path="${PRESSURE_VESSEL_SSH_PATH}" \
    )
elif [ -n "${PRESSURE_VESSEL_DOWNLOAD_URL-}" ]; then
    pressure_vessel_args=( \
        "${pressure_vessel_args[@]}" \
        --pressure-vessel-uri="${PRESSURE_VESSEL_DOWNLOAD_URL}" \
    )
fi

echo "1..3"

rm -fr depots/test-soldier-unpacked
mkdir -p depots/test-soldier-unpacked
python3 ./populate-depot.py \
    --depot=depots/test-soldier-unpacked \
    --depot-version=0.1.2.3 \
    --no-include-archives \
    --toolmanifest \
    --unpack-runtime \
    --versioned-directories \
    "${populate_depot_args[@]}" \
    "${pressure_vessel_args[@]}" \
    soldier \
    ${NULL+}
find depots/test-soldier-unpacked -ls > depots/test-soldier-unpacked.txt

if ! grep $'^depot\t0\\.1\\.2\\.3\t' depots/test-soldier-unpacked/VERSIONS.txt >/dev/null; then
    echo "Bail out! Depot version number not found"
    exit 1
fi

soldier_version="$(
    IFS="$(printf '\t')"
    while read -r component version runtime comment; do
        : "$runtime"    # unused
        : "$comment"    # unused
        if [ "$component" = "soldier" ]; then
            printf '%s' "$version"
        fi
    done < depots/test-soldier-unpacked/VERSIONS.txt
)"
if [ -z "$soldier_version" ]; then
    echo "Bail out! Unable to determine version"
    exit 1
fi

run_dir="$(sed -ne 's/^dir=//p' depots/test-soldier-unpacked/run)"
echo "# Expected: soldier_platform_$soldier_version"
echo "# In ./run: $run_dir"

if [ "$run_dir" != "soldier_platform_$soldier_version" ]; then
    exit 1
fi

run_dir="$(sed -ne 's/^dir=//p' depots/test-soldier-unpacked/run-in-soldier)"
echo "# Expected: soldier_platform_$soldier_version"
echo "# In ./run-in-soldier: $run_dir"

if [ "$run_dir" != "soldier_platform_$soldier_version" ]; then
    exit 1
fi

if ! [ -d "depots/test-soldier-unpacked/$run_dir" ]; then
    echo "Bail out! $run_dir not found"
    exit 1
fi

for dir in depots/test-soldier-unpacked/soldier*; do
    if [ -d "$dir" ] && [ "$dir" != "depots/test-soldier-unpacked/$run_dir" ]; then
        echo "Bail out! Unexpected directory $dir"
        exit 1
    fi
done

echo "ok 1 - soldier, running from unpacked directory"

# We rely on this having been downloaded into .cache by the previous run
ln .cache/com.valvesoftware.SteamRuntime.Platform-amd64,i386-soldier-runtime.tar.gz \
    depots/test-soldier-unpacked
echo "$soldier_version" > \
    depots/test-soldier-unpacked/com.valvesoftware.SteamRuntime.Platform-amd64,i386-soldier-buildid.txt

rm -fr depots/test-soldier-local
mkdir -p depots/test-soldier-local
python3 ./populate-depot.py \
    --depot=depots/test-soldier-local \
    --minimize \
    --no-include-archives \
    --toolmanifest \
    --unpack-runtime \
    --versioned-directories \
    "${populate_depot_args[@]}" \
    --pressure-vessel-archive=./.cache/pressure-vessel-bin.tar.gz \
    'soldier={"path": "./depots/test-soldier-unpacked"}' \
    ${NULL+}
find depots/test-soldier-local -ls > depots/test-soldier-local.txt

soldier_version="$(
    IFS="$(printf '\t')"
    while read -r component version runtime comment; do
        : "$runtime"    # unused
        : "$comment"    # unused
        if [ "$component" = "soldier" ]; then
            printf '%s' "$version"
        fi
    done < depots/test-soldier-local/VERSIONS.txt
)"
if [ -z "$soldier_version" ]; then
    echo "Bail out! Unable to determine version"
    exit 1
fi

case "$soldier_version" in
    (latest-*)
        echo "Bail out! $soldier_version is not a valid version"
        exit 1
        ;;
esac

run_dir="$(sed -ne 's/^dir=//p' depots/test-soldier-local/run)"
echo "# Expected: soldier_platform_$soldier_version"
echo "# In ./run: $run_dir"

if [ "$run_dir" != "soldier_platform_$soldier_version" ]; then
    exit 1
fi

run_dir="$(sed -ne 's/^dir=//p' depots/test-soldier-local/run-in-soldier)"
echo "# Expected: soldier_platform_$soldier_version"
echo "# In ./run-in-soldier: $run_dir"

if [ "$run_dir" != "soldier_platform_$soldier_version" ]; then
    exit 1
fi

if ! [ -d "depots/test-soldier-local/$run_dir" ]; then
    echo "Bail out! $run_dir not found"
    exit 1
fi

for dir in depots/test-soldier-local/soldier*; do
    if [ -d "$dir" ] && [ "$dir" != "depots/test-soldier-local/$run_dir" ]; then
        echo "Bail out! Unexpected directory $dir"
        exit 1
    fi
done

if ! [ -f "depots/test-soldier-local/$run_dir/files/.ref" ]; then
    echo "Bail out! files/.ref not created"
    exit 1
fi

empties="$(find "depots/test-soldier-local/$run_dir/files" -name .ref -prune -o -empty -print)"

if [ -n "$empties" ]; then
    echo "# Files not minimized:"
    # ${variable//search/replace} can't do this:
    # shellcheck disable=SC2001
    echo "$empties" | sed -e 's/^/# /'
    echo "Bail out! Files not minimized"
    exit 1
fi

symlinks="$(find "depots/test-soldier-local/$run_dir/files" -type l)"

if [ -n "$symlinks" ]; then
    echo "# Symlinks not minimized:"
    # shellcheck disable=SC2001
    echo "$symlinks" | sed -e 's/^/# /'
    echo "Bail out! Symlinks not minimized"
    exit 1
fi

echo "ok 2 - soldier, running from local builds"

rm -fr depots/test-soldier-unversioned
mkdir -p depots/test-soldier-unversioned
python3 ./populate-depot.py \
    --depot=depots/test-soldier-unversioned \
    --toolmanifest \
    --no-versioned-directories \
    "${populate_depot_args[@]}" \
    "${pressure_vessel_args[@]}" \
    soldier \
    ${NULL+}
find depots/test-soldier-unversioned -ls > depots/test-soldier-unversioned.txt

soldier_version="$(
    IFS="$(printf '\t')"
    while read -r component version runtime comment; do
        : "$runtime"    # unused
        : "$comment"    # unused
        if [ "$component" = "soldier" ]; then
            printf '%s' "$version"
        fi
    done < depots/test-soldier-unversioned/VERSIONS.txt
)"
if [ -z "$soldier_version" ]; then
    echo "Bail out! Unable to determine version"
    exit 1
fi

run_dir="$(sed -ne 's/^dir=//p' depots/test-soldier-unversioned/run)"
echo "# Expected: soldier"
echo "# In ./run: $run_dir"

if [ "$run_dir" != "soldier" ]; then
    exit 1
fi

run_dir="$(sed -ne 's/^dir=//p' depots/test-soldier-unversioned/run-in-soldier)"
echo "# Expected: soldier"
echo "# In ./run-in-soldier: $run_dir"

if [ "$run_dir" != "soldier" ]; then
    exit 1
fi

if ! [ -d "depots/test-soldier-unversioned/$run_dir" ]; then
    echo "Bail out! $run_dir not found"
    exit 1
fi

for dir in depots/test-soldier-unversioned/soldier*; do
    if [ -d "$dir" ] && [ "$dir" != "depots/test-soldier-unversioned/$run_dir" ]; then
        echo "Bail out! Unexpected directory $dir"
        exit 1
    fi
done

echo "ok 3 - soldier, running from unpacked directory without version"

# vim:set sw=4 sts=4 et:
