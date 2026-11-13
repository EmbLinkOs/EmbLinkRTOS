#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
# Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
#
# CI's package installation, bounded and retried: scripts/ci-apt-install.sh PACKAGE...
#
# The runners' Azure mirror has stalled in the middle of a download (gcc-arm-none-eabi's 57 MB,
# #32, #34, #35, #44, #64 to #73, and eleven jobs across #76 to #83 on 2026-11-05): apt waited
# until the job's timeout, and a re-run then passed. Each attempt here is cut after four minutes
# and retried. The third attempt uses archive.ubuntu.com instead of the Azure mirror, in the
# sources and in the mirror list the runners' sources point to (/etc/apt/apt-mirrors.txt, which
# the attempt missed before). Recommended packages are not installed: QEMU's alone pull pipewire,
# gstreamer and SDL, a third of the download, which no job needs.
#
# With CI_APT_CACHE set (.github/actions/apt-install, which restores and saves that directory
# with actions/cache), the archives are kept there: a later run with the same packages
# downloads only the indexes, unless the mirror has a newer version.
#
# Only the network is cut at four minutes: the indexes and the download. The installation that
# follows reads the downloaded archives and has ten minutes, as unpacking QEMU, the cross
# compilers and avr-libc can take longer than four on a slow runner. Cutting it there left dpkg
# interrupted, and every retry then failed at once ("dpkg was interrupted, you must manually run
# 'sudo dpkg --configure -a'", cm0-microbit-clang on #143); a retry now finishes an interrupted
# installation first.
set -u
[ $# -gt 0 ] || { echo "usage: $0 PACKAGE..." >&2; exit 2; }
APT="-o Acquire::Retries=3 -o Acquire::http::Timeout=30"
if [ -n "${CI_APT_CACHE:-}" ]; then
    sudo mkdir -p "$CI_APT_CACHE/partial"
    APT="$APT -o Dir::Cache::Archives=$CI_APT_CACHE -o APT::Keep-Downloaded-Packages=true"
    echo "ci-apt-install: $(ls "$CI_APT_CACHE"/*.deb 2>/dev/null | wc -l) archive(s) cached"
fi
cache_readable() {
    # actions/cache saves the directory as the runner's user, after apt wrote it as root
    [ -n "${CI_APT_CACHE:-}" ] || return 0
    sudo rm -f "$CI_APT_CACHE/lock"
    sudo chown -R "$(id -u):$(id -g)" "$CI_APT_CACHE"
}
attempt=1
while [ $attempt -le 3 ]; do
    if [ $attempt -eq 3 ]; then
        echo "ci-apt-install: attempt 3 from archive.ubuntu.com"
        for f in /etc/apt/apt-mirrors.txt /etc/apt/sources.list /etc/apt/sources.list.d/*.sources \
                 /etc/apt/sources.list.d/*.list; do
            [ -f "$f" ] && sudo sed -i 's|http://azure\.archive\.ubuntu\.com|http://archive.ubuntu.com|g' "$f"
        done
    fi
    if [ $attempt -gt 1 ]; then  # an attempt cut during installation leaves dpkg to finish
        sudo timeout 600 dpkg --configure -a || echo "ci-apt-install: dpkg --configure -a failed"
    fi
    # shellcheck disable=SC2086 # $APT is a list of options
    if sudo timeout 240 apt-get $APT update &&
       sudo timeout 240 apt-get $APT install -y --no-install-recommends --download-only "$@" &&
       sudo timeout 600 apt-get $APT install -y --no-install-recommends "$@"; then
        cache_readable
        exit 0
    fi
    echo "ci-apt-install: attempt $attempt failed or stalled; retrying"
    attempt=$((attempt + 1))
done
cache_readable
echo "ci-apt-install: three attempts failed" >&2
exit 1
