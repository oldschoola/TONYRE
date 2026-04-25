"""Decode PC Bink music (.bik) to .wav using ffmpeg.
Maps CRC-named .bik files to their real track stems from Scripts/game/skater/skater_sfx.q.
Outputs Data/music/<stem>.wav so TonyRE's miniaudio decoder can play them."""

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_BIK = ROOT / "Game" / "Data" / "streams" / "music"
DST_WAV = ROOT / "Game" / "Data" / "music"
SCRIPT = ROOT / "Scripts" / "game" / "skater" / "skater_sfx.q"

# Standard CRC32 table, poly 0xEDB88320
_TABLE = []
for i in range(256):
    c = i
    for _ in range(8):
        c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
    _TABLE.append(c)

def thug_crc(name: str) -> int:
    """THUG GenerateCRCFromString: reflected CRC32, seed 0xFFFFFFFF,
    no final XOR, lowercased, / -> \\."""
    rc = 0xFFFFFFFF
    for ch in name:
        if "A" <= ch <= "Z":
            ch = ch.lower()
        elif ch == "/":
            ch = "\\"
        rc = _TABLE[(rc ^ ord(ch)) & 0xFF] ^ ((rc >> 8) & 0x00FFFFFF)
    return rc & 0xFFFFFFFF

def parse_playlist(text: str):
    """Yield (path, basename) from playlist_tracks block."""
    m = re.search(r"playlist_tracks\s*=\s*\[(.*?)\]", text, re.S)
    if not m:
        return
    block = m.group(1)
    for path in re.findall(r'path\s*=\s*"([^"]+)"', block):
        base = path.replace("\\\\", "\\").replace("/", "\\").split("\\")[-1]
        yield path.replace("\\\\", "\\"), base

def main():
    DST_WAV.mkdir(parents=True, exist_ok=True)
    text = SCRIPT.read_text(encoding="utf-8", errors="replace")

    tracks = list(parse_playlist(text))
    print(f"Found {len(tracks)} playlist tracks", flush=True)

    missing = []
    converted = 0
    skipped = 0

    for path, stem in tracks:
        crc = thug_crc(stem)
        bik = SRC_BIK / f"{crc:08x}.bik"
        wav = DST_WAV / f"{stem}.wav"

        if not bik.exists():
            missing.append((path, stem, f"{crc:08x}"))
            continue

        if wav.exists() and wav.stat().st_size > 0:
            skipped += 1
            continue

        print(f"[{converted + skipped + 1}/{len(tracks)}] {stem}  <-  {bik.name}", flush=True)
        r = subprocess.run(
            ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
             "-i", str(bik), "-vn", "-acodec", "pcm_s16le", str(wav)],
            capture_output=True, text=True,
        )
        if r.returncode != 0:
            print(f"  ffmpeg failed: {r.stderr.strip()}", flush=True)
            if wav.exists():
                wav.unlink()
            continue
        converted += 1

    print(f"\nConverted: {converted}  Skipped(existing): {skipped}  Missing: {len(missing)}")
    if missing:
        print("Missing .bik for:")
        for path, stem, hexc in missing[:20]:
            print(f"  {stem:30s}  crc={hexc}  path={path}")

if __name__ == "__main__":
    sys.exit(main() or 0)
