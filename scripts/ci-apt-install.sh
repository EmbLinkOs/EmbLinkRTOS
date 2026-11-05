#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
#
# CI's package installation, bounded and retried: scripts/ci-apt-install.sh PACKAGE...
#
# The runners' Azure mirror has stalled in the middle of a download (gcc-arm-none-eabi's 57 MB,
# #32, #34, #35, #44, #64 to #73): apt waited until the job's 15-minute timeout, and a re-run
# then passed. Each attempt here is cut after four minutes and retried. The third attempt uses
# archive.ubuntu.com instead of the Azure mirror. Recommended packages are not installed: QEMU's
# alone pull pipewire, gstreamer and SDL, a third of the download, which no job needs.
set -u
[ $# -gt 0 ] || { echo "usage: $0 PACKAGE..." >&2; exit 2; }
APT="-o Acquire::Retries=3 -o Acquire::http::Timeout=30"
attempt=1
while [ $attempt -le 3 ]; do
    if [ $attempt -eq 3 ]; then
        echo "ci-apt-install: attempt 3 from archive.ubuntu.com"
        for f in /etc/apt/sources.list /etc/apt/sources.list.d/*.sources /etc/apt/sources.list.d/*.list; do
            [ -f "$f" ] && sudo sed -i 's|http://azure\.archive\.ubuntu\.com|http://archive.ubuntu.com|g' "$f"
        done
    fi
    # shellcheck disable=SC2086 # $APT is a list of options
    if sudo timeout 240 apt-get $APT update &&
       sudo timeout 240 apt-get $APT install -y --no-install-recommends "$@"; then
        exit 0
    fi
    echo "ci-apt-install: attempt $attempt failed or stalled; retrying"
    attempt=$((attempt + 1))
done
echo "ci-apt-install: three attempts failed" >&2
exit 1
