#!/usr/bin/env bash
# Prepare the build container: rewrite apt sources for EOL Debian
# releases and install the toolchain needed to build the package.
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

if [[ ! -r /etc/os-release ]]; then
    echo "Missing /etc/os-release; cannot detect distribution"
    exit 1
fi

# shellcheck disable=SC1091
source /etc/os-release

ID="${ID:-}"
VERSION_CODENAME="${VERSION_CODENAME:-}"

if [[ -n "${ID}" && -z "${VERSION_CODENAME}" ]]; then
    # EOL Debian releases (e.g. jessie) carry no VERSION_CODENAME; the
    # codename only appears parenthesized in VERSION ("8 (jessie)")
    VERSION_CODENAME="$(sed -n 's/.*(\([^()]*\)).*/\1/p' <<< "${VERSION:-}")"
fi

if [[ -z "${ID}" || -z "${VERSION_CODENAME}" ]]; then
    echo "Could not detect distribution ID or VERSION_CODENAME"
    exit 1
fi

if [[ "${ID}" == "debian" ]]; then
    case "${VERSION_CODENAME}" in
        jessie)
            # All repos live on archive.debian.org; the image's keyring
            # has expired keys, hence trusted=yes.  jessie-backports is
            # needed for debhelper >= 10 (compat level 10).
            printf '%s\n' \
                'deb [trusted=yes] http://archive.debian.org/debian jessie main' \
                'deb [trusted=yes] http://archive.debian.org/debian jessie-backports main' \
                'deb [trusted=yes] http://archive.debian.org/debian-security jessie/updates main' \
                > /etc/apt/sources.list
            rm -f /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources
            ;;
        stretch)
            printf '%s\n' \
                'deb [trusted=yes] http://archive.debian.org/debian stretch main' \
                'deb [trusted=yes] http://archive.debian.org/debian-security stretch/updates main' \
                > /etc/apt/sources.list
            rm -f /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources
            ;;
        buster)
            printf '%s\n' \
                'deb [trusted=yes] http://archive.debian.org/debian buster main' \
                'deb [trusted=yes] http://archive.debian.org/debian buster-updates main' \
                'deb [trusted=yes] http://archive.debian.org/debian-security buster/updates main' \
                > /etc/apt/sources.list
            rm -f /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources
            ;;
        bullseye)
            # bullseye-security was purged from the CDN but is not on the
            # archive yet; use the last good snapshot for it.
            printf '%s\n' \
                'deb [trusted=yes] http://archive.debian.org/debian bullseye main' \
                'deb [trusted=yes] http://archive.debian.org/debian bullseye-updates main' \
                'deb [trusted=yes] http://snapshot.debian.org/archive/debian-security/20260901T000000Z bullseye-security main' \
                > /etc/apt/sources.list
            rm -f /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources
            ;;
    esac

    # archived Release files carry an expired Valid-Until (jessie's apt
    # predates the check entirely, so this is harmless there)
    echo 'Acquire::Check-Valid-Until "false";' \
        > /etc/apt/apt.conf.d/99eol-archive
fi

apt-get update

# debhelper >= 10 is only in jessie-backports; -t lifts the pin so the
# versioned Build-Depends can be satisfied.
if [[ "${ID}" == "debian" && "${VERSION_CODENAME}" == "jessie" ]]; then
    apt-get -y install --no-install-recommends -t jessie-backports debhelper
fi

apt-get -y install --no-install-recommends \
    debhelper \
    dpkg-dev \
    build-essential \
    libevent-dev \
    python3 \
    procps
