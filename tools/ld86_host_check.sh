#!/usr/bin/env sh
set -eu

echo "[ld86-host] checking archive format (libtiny.a)"
python3 - <<'PY'
from pathlib import Path
p = Path("userland/libtiny.a").read_bytes()
assert p.startswith(b"!<arch>\n"), "bad ar magic"
off = 8
members = []
while off + 60 <= len(p):
    hdr = p[off:off+60]
    size = int(hdr[48:58].decode("ascii").strip() or "0")
    name = hdr[:16].decode("ascii", "ignore").strip()
    members.append(name)
    off = off + 60 + size
    if off & 1:
        off += 1
assert off == len(p), f"archive parse mismatch: off={off} len={len(p)}"
assert any(n.startswith("libtiny.o") for n in members), "libtiny.o missing"
print("[ld86-host] archive members:", ", ".join(members))
PY

echo "[ld86-host] checking crt0 relocation"
readelf -r userland/crt0.o | grep -q 'R_386_PC32.*\$main'

echo "[ld86-host] checking libc exports \$print alias"
readelf -s userland/libc.o | grep -q '\$print'

echo "[ld86-host] PASS"
