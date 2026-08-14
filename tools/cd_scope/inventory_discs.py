"""Inventory the FDPS discs by copying them out through DOSBox-X.

The .cue is imgmounted as E: and the work directory is mounted as F:, then a
plain DOS COPY pulls the whole disc out. Sizes and SHA-256 are computed on the
Windows side afterwards. Audio track layout comes from the TRACK/INDEX records
in the .cue; the last track's length is derived from the .bin file size.

Usage:
    python inventory_discs.py copy   <cue path> <output dir>
    python inventory_discs.py report <cue path> <output dir> <json path>
"""

import hashlib
import json
import subprocess
import sys
from pathlib import Path

DOSBOX = Path(r"C:\DOSBox-X\dosbox-x.exe")
RAW_SECTOR = 2352


def msf_to_lba(mm, ss, ff):
    return (mm * 60 + ss) * 75 + ff


def parse_cue_tracks(cue_path):
    """Return [(track_no, mode, lba)] from the .cue text."""
    tracks = []
    track_no = mode = None
    for line in Path(cue_path).read_text(encoding="utf-8", errors="replace").splitlines():
        s = line.strip()
        if s.startswith("TRACK "):
            _, track_no, mode = s.split()[:3]
            track_no = int(track_no)
        elif s.startswith("INDEX 01") and track_no is not None:
            mm, ss, ff = (int(v) for v in s.split()[2].split(":"))
            tracks.append((track_no, mode, msf_to_lba(mm, ss, ff)))
    return tracks


def copy_disc(cue_path, out_dir):
    """Copy the whole disc into out_dir from inside DOSBox-X."""
    out_dir.mkdir(parents=True, exist_ok=True)
    conf = out_dir.parent / f"{out_dir.name}_copy.conf"
    conf.write_text(
        "[dosbox]\n"
        "memsize=32\n"
        "[sdl]\n"
        "autolock=false\n"
        "[autoexec]\n"
        f'mount f "{out_dir}"\n'
        f'imgmount e -t cdrom "{cue_path}"\n'
        "f:\n"
        "copy e:\\*.* f:\\\n"
        "exit\n",
        encoding="utf-8",
    )
    subprocess.run(
        [str(DOSBOX), "-silent", "-exit", "-conf", str(conf)],
        check=True,
        timeout=3600,
    )
    conf.unlink()
    print(f"{cue_path.name}: copied {len(list(out_dir.iterdir()))} files to {out_dir}")


def report(cue_path, src_dir, json_path):
    """Inventory the copied files and the .cue audio track table into JSON."""
    files = []
    for p in sorted(src_dir.iterdir(), key=lambda x: x.name.upper()):
        if not p.is_file():
            continue
        h = hashlib.sha256()
        with open(p, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                h.update(chunk)
        files.append({"name": p.name.upper(), "size": p.stat().st_size, "sha256": h.hexdigest()})

    tracks = parse_cue_tracks(cue_path)
    total_sectors = cue_path.with_suffix(".bin").stat().st_size // RAW_SECTOR
    audio = []
    for i, (no, mode, lba) in enumerate(tracks):
        if mode != "AUDIO":
            continue
        end = tracks[i + 1][2] if i + 1 < len(tracks) else total_sectors
        audio.append(
            {
                "track": no,
                "start_lba": lba,
                "frames": end - lba,
                "seconds": round((end - lba) / 75.0, 2),
            }
        )

    json_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(
        json.dumps(
            {
                "image": cue_path.with_suffix(".bin").name,
                "total_raw_sectors": total_sectors,
                "files": files,
                "audio_tracks": audio,
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )
    print(f"{cue_path.name}: {len(files)} files, {len(audio)} audio tracks -> {json_path}")


def main():
    cmd = sys.argv[1]
    if cmd == "copy":
        copy_disc(Path(sys.argv[2]), Path(sys.argv[3]))
    elif cmd == "report":
        report(Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
