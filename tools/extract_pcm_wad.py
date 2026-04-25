"""Extract PC pcm.wad voice streams.
Each pcm.dat entry is 12 bytes: CRC(u32), offset(u32), size(u32).
Each chunk at pcm.wad[offset:offset+size] is a standalone Bink file.
Decode with ffmpeg to Data/streams/pcm/<hex_crc>.wav.
The TonyRE Index() patch accepts hex-stem filenames directly as their checksum,
so lookups by the game work without needing the original stream names."""

import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PCM_DIR = ROOT / "Game" / "Data" / "streams" / "pcm"
DAT = PCM_DIR / "pcm.dat"
WAD = PCM_DIR / "pcm.wad"
OUT_DIR = PCM_DIR
TMP_ROOT = Path(tempfile.mkdtemp(prefix="pcm_wad_"))

def main():
    data = DAT.read_bytes()
    (count,) = struct.unpack_from("<I", data, 0)
    print(f"{count} entries")

    entries = []
    off = 4
    for i in range(count):
        crc, pos, size = struct.unpack_from("<III", data, off)
        off += 12
        entries.append((crc, pos, size))

    ok = 0
    fail = 0
    skip = 0

    with WAD.open("rb") as wad:
        for idx, (crc, pos, size) in enumerate(entries):
            out = OUT_DIR / f"{crc:08x}.wav"
            if out.exists() and out.stat().st_size > 0:
                skip += 1
                continue

            wad.seek(pos)
            blob = wad.read(size)
            tmp = TMP_ROOT / f"{crc:08x}.bik"
            tmp.write_bytes(blob)

            r = subprocess.run(
                ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                 "-i", str(tmp), "-vn", "-acodec", "pcm_s16le", str(out)],
                capture_output=True, text=True,
            )
            tmp.unlink(missing_ok=True)
            if r.returncode != 0 or not out.exists() or out.stat().st_size == 0:
                fail += 1
                if out.exists():
                    out.unlink()
                if fail <= 5:
                    print(f"  fail {crc:08x}: {r.stderr.strip()[:120]}")
            else:
                ok += 1

            if (idx + 1) % 200 == 0:
                print(f"[{idx + 1}/{count}] ok={ok} fail={fail} skip={skip}", flush=True)

    print(f"\nDone: ok={ok} fail={fail} skip={skip}")
    try:
        TMP_ROOT.rmdir()
    except OSError:
        pass

if __name__ == "__main__":
    sys.exit(main() or 0)
