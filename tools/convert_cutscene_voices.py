"""Decode PC cutscene voice Bink (.bik) to .wav.
Engine calls Pcm::PreLoadMusicStream(CRC("<cut>_Male"|"_Female")) from
CCutsceneData::Load, which looks up music_paths (indexed from Data/music/).
PC ships cutscene voices as hex-named .bik in Data/streams/music/ alongside
the music tracks, so we CRC each cutscene name + sex suffix, find the
matching .bik, and decode to Data/music/<cut>_<Male|Female>.wav."""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from convert_music import thug_crc  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
CUT_DIR = ROOT / "Game" / "Data" / "cutscenes"
SRC_BIK = ROOT / "Game" / "Data" / "streams" / "music"
DST_WAV = ROOT / "Game" / "Data" / "music"

def main():
    DST_WAV.mkdir(parents=True, exist_ok=True)
    cuts = sorted(p.stem for p in CUT_DIR.glob("*.cut"))
    print(f"Found {len(cuts)} cutscenes", flush=True)

    converted = skipped = missing = failed = 0
    missing_list = []

    for cut in cuts:
        for sex in ("Male", "Female"):
            stream = f"{cut}_{sex}"
            crc = thug_crc(stream)
            bik = SRC_BIK / f"{crc:08x}.bik"
            wav = DST_WAV / f"{stream}.wav"

            if not bik.exists():
                missing += 1
                missing_list.append((stream, f"{crc:08x}"))
                continue

            if wav.exists() and wav.stat().st_size > 0:
                skipped += 1
                continue

            print(f"  {stream}  <-  {bik.name}", flush=True)
            r = subprocess.run(
                ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                 "-i", str(bik), "-vn", "-acodec", "pcm_s16le", str(wav)],
                capture_output=True, text=True,
            )
            if r.returncode != 0:
                failed += 1
                print(f"    ffmpeg: {r.stderr.strip()[:120]}", flush=True)
                if wav.exists():
                    wav.unlink()
                continue
            converted += 1

    print(f"\nConverted: {converted}  Skipped(existing): {skipped}"
          f"  Missing: {missing}  Failed: {failed}")
    for stream, hexc in missing_list[:10]:
        print(f"  missing bik for {stream}  crc={hexc}")

if __name__ == "__main__":
    sys.exit(main() or 0)
