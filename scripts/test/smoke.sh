#!/usr/bin/env bash
# Smoke test: run the just-built daemon and proxy a SOCKS5 request
# through it end to end.
set -euo pipefail

SCRIPT=$(readlink -f "$0")
SCRIPTDIR=$(dirname "${SCRIPT}")
WORKDIR="${PWD}"

# shellcheck source=../common.sh
source "${SCRIPTDIR}/../common.sh"

# pick the package built for this container's distribution (several may
# coexist in WORKDIR during parallel local builds)
DISTRO_CODENAME=$( . /etc/os-release 2>/dev/null; echo "${VERSION_CODENAME:-}" )
if [ -z "${DISTRO_CODENAME}" ]; then
    DISTRO_CODENAME=$( . /etc/os-release 2>/dev/null; echo "${VERSION:-}" \
        | sed -n 's/.*(\([^()]*\)).*/\1/p' )
fi
if [ -n "${DISTRO_CODENAME}" ]; then
    DEB=$(ls "${WORKDIR}"/nylon_*"+${DISTRO_CODENAME}"_amd64.deb 2>/dev/null | head -n1 || true)
else
    DEB=$(ls "${WORKDIR}"/nylon_*_amd64.deb 2>/dev/null | head -n1 || true)
fi
[ -n "${DEB}" ] || { errorline "no built .deb found"; exit 1; }

statusline "Installing ${DEB}"
dpkg -i "${DEB}" || apt-get install -y -f

statusline "Running SOCKS5 smoke test"
/usr/sbin/nylon -f -v -i 127.0.0.1 -p 11080 -P /tmp/nylon-smoke.pid \
    -a 127.0.0.1/32 2>/tmp/nylon-smoke.log &
DPID=$!
trap 'kill ${DPID} 2>/dev/null || true' EXIT

# wait for the listen socket instead of a fixed sleep
for _ in $(seq 1 50); do
    kill -0 "${DPID}" 2>/dev/null || break
    (exec 3<>/dev/tcp/127.0.0.1/11080) 2>/dev/null && { exec 3>&- 3<&-; break; }
    sleep 0.2
done
kill -0 "${DPID}" || { errorline "daemon exited"; cat /tmp/nylon-smoke.log; exit 1; }

if ! python3 - <<'PYEOF'
import socket, struct, sys

# echo server as the upstream target
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 18080))
srv.listen(1)

import threading
def echo_once():
    c, _ = srv.accept()
    d = c.recv(100)
    c.sendall(d)
    c.close()
threading.Thread(target=echo_once, daemon=True).start()

s = socket.socket()
s.connect(("127.0.0.1", 11080))
s.sendall(bytes([5, 1, 0]))
assert s.recv(2) == bytes([5, 0]), "method negotiation failed"
s.sendall(bytes([5, 1, 0, 3, 9]) + b"localhost" + struct.pack("!H", 18080))
r = s.recv(10)
assert r[1] == 0, "CONNECT failed, rep=%d" % r[1]
s.sendall(b"smoke")
assert s.recv(10) == b"smoke", "no data relayed"
s.close()
print("smoke: SOCKS5 FQDN CONNECT + relay OK")
PYEOF
then
    errorline "SOCKS5 smoke test failed"
    cat /tmp/nylon-smoke.log
    exit 1
fi

kill ${DPID} 2>/dev/null
trap - EXIT
cat /tmp/nylon-smoke.log
statusline "Smoke test passed"
