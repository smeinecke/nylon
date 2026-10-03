#!/usr/bin/env bash
# Build the nylon .deb inside the target distro container.
# The source tree is self-contained (upstream + debian/ in one repo),
# so this is just a dpkg-buildpackage run.
set -euo pipefail

SCRIPT=$(readlink -f "$0")
SCRIPTDIR=$(dirname "${SCRIPT}")
WORKDIR="${PWD}"

# shellcheck source=common.sh
source "${SCRIPTDIR}/common.sh"

# detect the build host distribution: the codename is appended to the
# package version so artifacts and repo entries stay unique per distro
DISTRO_CODENAME=$( . /etc/os-release 2>/dev/null; echo "${VERSION_CODENAME:-}" )
if [ -z "${DISTRO_CODENAME}" ]; then
    # EOL Debian releases carry no VERSION_CODENAME; the codename only
    # appears parenthesized in VERSION ("8 (jessie)")
    DISTRO_CODENAME=$( . /etc/os-release 2>/dev/null; echo "${VERSION:-}" \
        | sed -n 's/.*(\([^()]*\)).*/\1/p' )
fi

statusline "Building nylon package for ${DISTRO_CODENAME:-unknown}"

# work on a copy so generated build artifacts never dirty the checkout
BUILD_DIR=/tmp/nylon-build
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cp -a "${WORKDIR}"/. "${BUILD_DIR}"/
cd "${BUILD_DIR}"

if [ -n "${DISTRO_CODENAME}" ]; then
    sed -i "1s/)/+${DISTRO_CODENAME})/" debian/changelog
    statusline "Version: $(sed -n '1p' debian/changelog)"
fi

DEB_BUILD_OPTIONS=noautodbgsym dpkg-buildpackage -us -uc -b

# dpkg-buildpackage drops the .deb in the parent of the build dir
mv /tmp/nylon_*_*.deb "${WORKDIR}"/
statusline "Built: $(basename "${WORKDIR}"/nylon_*.deb)"
