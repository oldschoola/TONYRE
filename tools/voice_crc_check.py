"""Check whether voice names from scripts CRC-match pcm.dat entries."""
import struct
from pathlib import Path

ROOT = Path(r"D:\_thug\TONYRE")
DAT = ROOT / "Game" / "Data" / "streams" / "pcm" / "pcm.dat"

data = DAT.read_bytes()
(count,) = struct.unpack_from("<I", data, 0)
crcs = set()
for i in range(count):
    crc, _, _ = struct.unpack_from("<III", data, 4 + i * 12)
    crcs.add(f"{crc:08x}")
print(f"pcm.dat has {len(crcs)} unique CRCs")

_TBL = []
for i in range(256):
    c = i
    for _ in range(8):
        c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
    _TBL.append(c)

def tc(s: str) -> int:
    rc = 0xFFFFFFFF
    for ch in s:
        if "A" <= ch <= "Z":
            ch = ch.lower()
        elif ch == "/":
            ch = "\\"
        rc = _TBL[(rc ^ ord(ch)) & 0xFF] ^ ((rc >> 8) & 0x00FFFFFF)
    return rc & 0xFFFFFFFF

names = [
    "TombstoneManInspect",
    "NJ_DogBark03",
    "NJ_DogBark04",
    "TombstoneManWipeBrow",
    "StandFromChisel",
    "TombstoneManStandToKneel",
]
for n in names:
    h = f"{tc(n):08x}"
    print(f"{n:30s}  crc={h}  hit={h in crcs}")
