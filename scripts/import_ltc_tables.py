"""Import pinned GGX LTC fits; run from repository root. Builds never access the network."""
from pathlib import Path
import hashlib
import re
import struct
import urllib.request

REVISION = "31e5e96b54f98f33098f8503003119ba2231a1c6"
SOURCE_SHA256 = "21071160163defd419b8f754ab604a8e60c44fcf539bfff9916e0e8e82316c1d"
BASE = "https://raw.githubusercontent.com/selfshadow/ltc_code/" + REVISION + "/"
raw = urllib.request.urlopen(BASE + "fit/results/ltc.js", timeout=60).read()
if hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
    raise RuntimeError("Pinned LTC source checksum mismatch")
tables = []
for index in (1, 2):
    match = re.search(r"var g_ltc_" + str(index) + r" = \[(.*?)\];", raw.decode(), re.S)
    values = [float(value) for value in match.group(1).split(",") if value.strip()]
    if len(values) != 4096 * 4:
        raise RuntimeError("Unexpected LTC table extent")
    tables.append(values)
lines = [
    "// GGX LTC fit: Heitz, Dupuy, Hill and Neubelt, SIGGRAPH 2016.",
    "// Source: https://github.com/selfshadow/ltc_code/blob/" + REVISION + "/fit/results/ltc.js",
    "// Source SHA256: " + SOURCE_SHA256,
    "// License: ThirdParty/ltc_code/LICENSE. Reproduce: python scripts/import_ltc_tables.py",
    "// One packed cell = half (m00,m02,m20,m22,norm,Fresnel). <=64KiB including uint3 alignment.",
    "static const LTC_UINT3 kLTCPacked[4096] = {",
]
for index in range(4096):
    matrix = tables[0][index * 4 : index * 4 + 4]
    amplitude = tables[1][index * 4 : index * 4 + 4]
    values = [matrix[0], matrix[2], matrix[1], matrix[3], amplitude[0], amplitude[1]]
    packed = [
        int.from_bytes(struct.pack("<ee", values[offset], values[offset + 1]), "little")
        for offset in (0, 2, 4)
    ]
    lines.append("    LTC_UINT3(" + ", ".join("0x%08xu" % value for value in packed) + "),")
lines.append("};")
Path("Engine/Resources/Shaders/LTCTables.inc").write_bytes(("\n".join(lines) + "\n").encode())
license_dir = Path("ThirdParty/ltc_code")
license_dir.mkdir(parents=True, exist_ok=True)
license_text = urllib.request.urlopen(BASE + "LICENSE", timeout=60).read()
# Normalize trailing whitespace only; retain the complete upstream notice and terms.
(license_dir / "LICENSE").write_bytes(b"\n".join(line.rstrip() for line in license_text.splitlines()) + b"\n")
print("Imported pinned 64x64 GGX LTC fit:", REVISION)
