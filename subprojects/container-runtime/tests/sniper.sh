#!/bin/bash
# Copyright © 2021 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eu

if [ -n "${TESTS_ONLY-}" ]; then
    echo "1..0 # SKIP This distro is too old to run populate-depot.py"
    exit 0
fi

populate_depot_args=( \
    "--scripts-version=test test" \
    "--steam-app-id=1628350" \
    "--steam-depot-id=1628351" \
)

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

echo "1..1"

rm -fr depots/test-sniper-unpacked
mkdir -p depots/test-sniper-unpacked
python3 ./populate-depot.py \
    --depot=depots/test-sniper-unpacked \
    --depot-archive=depots/SteamLinuxRuntime_sniper.tar.xz \
    --fast \
    --no-include-archives \
    --toolmanifest \
    --unpack-runtime \
    --versioned-directories \
    "${populate_depot_args[@]}" \
    "${pressure_vessel_args[@]}" \
    sniper \
    ${NULL+}
find depots/test-sniper-unpacked -ls > depots/test-sniper-unpacked.txt
if ! [ -e "depots/test-sniper-unpacked/steampipe/app_build_1628350.vdf" ]; then
    echo "Bail out! app_build_1628350.vdf not found"
    exit 1
fi
if ! [ -e "depots/test-sniper-unpacked/steampipe/depot_build_1628351.vdf" ]; then
    echo "Bail out! depot_build_1628351.vdf not found"
    exit 1
fi
echo "ok 1 - sniper, running from unpacked directory"

# vim:set sw=4 sts=4 et:
