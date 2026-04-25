"""Brute-force scan: find any string in source/scripts/data whose THUG CRC
matches an unmapped .bik filename. Extends music mapping beyond playlist_tracks."""
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from convert_music import thug_crc  # noqa: E402

ROOT = Path("D:/_thug/TONYRE")
BIK_DIR = ROOT / "Game" / "Data" / "streams" / "music"
SKIP_DIRS = {"build", ".git", "External", "GameSpy", "node_modules"}
EXTS = {".q", ".qb", ".h", ".cpp", ".c", ".txt", ".inl"}

biks = {p.stem for p in BIK_DIR.iterdir() if p.suffix == ".bik"}
print(f"{len(biks)} .bik files")

matches: dict[str, str] = {}
for root, dirs, files in os.walk(ROOT):
    dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
    for f in files:
        if os.path.splitext(f)[1].lower() not in EXTS:
            continue
        try:
            text = (Path(root) / f).read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for s in re.findall(r'"([^"]{1,80})"', text):
            h = f"{thug_crc(s):08x}"
            if h in biks and h not in matches.values():
                matches[s] = h

print(f"matched {len(matches)} strings to .bik files")
base_map: dict[str, tuple[str, str]] = {}
for s, h in matches.items():
    base = s.replace("/", "\\").split("\\")[-1]
    if base and base not in base_map:
        base_map[base] = (h, s)

for base in sorted(base_map):
    h, s = base_map[base]
    print(f"  {h}  base={base:25s}  full={s}")
