# nylon

nylon is a lightweight proxy daemon, originally by Marius Aamodt
Eriksen, supporting SOCKS version 4, 4a and 5 as well as a simple
services mirror mode.  It is written in C on top of libevent and
handles each connection in a forked child process.

## Features

- SOCKS4 / SOCKS4A / SOCKS5 CONNECT and BIND, plus a mirror mode that
  forwards connections to a fixed destination
- Client-side allow/deny ACLs using CIDR notation
- Optional destination filtering (`Allow-Target-IP` / `Deny-Target-IP`)
  to keep clients away from loopback, link-local or internal networks
- Privilege dropping to an unprivileged user when started as root
- Configurable connection limit (`No-Simultaneous-Conn`)
- SIGHUP-triggered re-exec to reload configuration
- IPv4 only

## Debian packages

Ready-built `.deb` packages for Debian jessie, stretch, buster,
bullseye, bookworm and trixie are produced by the GitHub Actions
workflow in this repository (see the *Actions* tab / artifacts of the
latest *Build package* run).  Each package is built inside a container
of its target release, so runtime dependencies match the distribution
(e.g. `libevent-2.0-5` on jessie/stretch vs `libevent-2.1-7t64` on
trixie).

The set of target images lives in `.github/supported-releases.txt`;
each CI job runs `scripts/ci/setup-build-env.sh`, `scripts/build.sh`
and `scripts/test/smoke.sh` inside a container of that image, so the
same build can be reproduced locally:

```sh
docker run --rm -v "$PWD:/workspace" -w /workspace debian:bookworm \
    bash -c './scripts/ci/setup-build-env.sh && ./scripts/build.sh && ./scripts/test/smoke.sh'
```

The produced `nylon_<version>+<dist>_amd64.deb` lands in the working
directory.  Tag pushes (`v*`) additionally publish the packages as a
GitHub release and, when the `APT_SIGNING_KEY` repository secret is
configured, to the apt repository.

To build the package yourself on a Debian system:

```sh
apt-get install build-essential debhelper libevent-dev
dpkg-buildpackage -us -uc -b
```

The daemon is installed as `/usr/sbin/nylon`; the shipped
configuration is `/etc/nylon.conf`, defaults for the service live in
`/etc/default/nylon`, and both a SysV init script and a systemd unit
are provided.

## Building from source

```sh
./configure
make
make install
```

Requires libevent development headers (`libevent-dev`).

## Usage

```sh
# SOCKS4+5 proxy on localhost:1080, localhost clients only
nylon -i 127.0.0.1 -p 1080 -a 127.0.0.1/32

# run in the foreground with verbose logging
nylon -f -v -i 0.0.0.0 -p 1080 -a 10.0.0.0/8 -d 192.168.1.0/24
```

See `nylon -h`, the `nylon(1)` manpage and the commented
`/etc/nylon.conf` for all options.

## License

nylon is distributed under a BSD-like license.  See the LICENSE file
for more information.
