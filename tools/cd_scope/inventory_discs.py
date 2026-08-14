"""Inventory the FDPS discs by copying them out through DOSBox-X.

The .cue is imgmounted as E: and the work directory is mounted as F:, then a
DOS XCOPY pulls the whole disc out. Sizes and SHA-256 are computed on the
Windows side afterwards. Audio track layout comes from the TRACK/INDEX records
in the .cue; the last track's length is derived from the .bin file size.

DOSBox-X exits with status 0 whether or not IMGMOUNT and XCOPY succeeded, so a
failed mount would otherwise yield an empty directory and a plausible-looking
report. Every copy is therefore checked against the ISO9660 root directory read
straight out of the image, which is the authoritative file list.

Usage:
    python inventory_discs.py copy   <cue path> <output dir>
    python inventory_discs.py report <cue path> <output dir> <json path>
"""

import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

DOSBOX = Path(r"C:\DOSBox-X\dosbox-x.exe")
RAW_SECTOR = 2352  # MODE1/2352 data sectors and AUDIO frames are both this size
USER_DATA = 2048  # payload carried inside a MODE1/2352 sector
USER_OFFSET = 16  # sync pattern and header ahead of the payload
ISO_PVD_LBA = 16
DIR_FLAG = 0x02


def msf_to_lba(mm, ss, ff):
    return (mm * 60 + ss) * 75 + ff


def parse_cue(cue_path):
    """Return (bin path, [(track_no, mode, lba)]) from the .cue text.

    Only single-FILE cues are supported. With one .bin per track the INDEX
    times are per-file offsets rather than one disc-wide timeline, and treating
    them as a timeline would silently produce wrong track lengths.
    """
    cue_path = Path(cue_path)
    bin_names, tracks = [], []
    track_no = mode = None
    for line in cue_path.read_text(encoding="utf-8", errors="replace").splitlines():
        stripped = line.strip()
        parts = stripped.split()
        if not parts:
            continue
        key = parts[0].upper()
        if key == "FILE":
            quoted = stripped.split('"')
            bin_names.append(quoted[1] if len(quoted) >= 3 else parts[1])
        elif key == "TRACK" and len(parts) >= 3:
            track_no, mode = int(parts[1]), parts[2].upper()
        elif key == "INDEX" and track_no is not None and int(parts[1]) == 1:
            mm, ss, ff = (int(v) for v in parts[2].split(":"))
            tracks.append((track_no, mode, msf_to_lba(mm, ss, ff)))

    if len(bin_names) != 1:
        raise SystemExit(
            f"{cue_path.name}: expected exactly one FILE directive, found {len(bin_names)}"
        )
    if not tracks:
        raise SystemExit(f"{cue_path.name}: no INDEX 01 records found")
    for _, mode, _ in tracks:
        if "/" in mode and mode.split("/", 1)[1] != str(RAW_SECTOR):
            raise SystemExit(
                f"{cue_path.name}: track mode {mode} is not a {RAW_SECTOR}-byte sector"
            )
    return cue_path.parent / bin_names[0], tracks


def read_sectors(fh, lba, count):
    """Read the user data of count consecutive MODE1/2352 sectors."""
    out = bytearray()
    for i in range(count):
        fh.seek((lba + i) * RAW_SECTOR + USER_OFFSET)
        out += fh.read(USER_DATA)
    return bytes(out)


def iso_root_entries(bin_path):
    """Return {NAME: size} for every file in the image's ISO9660 root directory."""
    with open(bin_path, "rb") as fh:
        pvd = read_sectors(fh, ISO_PVD_LBA, 1)
        if pvd[1:6] != b"CD001":
            raise SystemExit(
                f"{bin_path.name}: no ISO9660 primary volume descriptor at LBA {ISO_PVD_LBA}"
            )
        root = pvd[156:190]
        root_lba = struct.unpack("<I", root[2:6])[0]
        root_len = struct.unpack("<I", root[10:14])[0]
        data = read_sectors(fh, root_lba, (root_len + USER_DATA - 1) // USER_DATA)

    entries = {}
    pos = 0
    while pos < len(data):
        rec_len = data[pos]
        if rec_len == 0:  # the rest of this sector is padding
            pos = (pos // USER_DATA + 1) * USER_DATA
            continue
        rec = data[pos:pos + rec_len]
        if not rec[25] & DIR_FLAG:  # directories, including . and .., are skipped
            name = rec[33:33 + rec[32]].split(b";")[0].decode("ascii", "replace")
            entries[name.upper()] = struct.unpack("<I", rec[10:14])[0]
        pos += rec_len
    return entries


def verify_against_image(bin_path, out_dir):
    """Fail loudly if out_dir does not hold every file the image's directory lists."""
    expected = iso_root_entries(bin_path)
    actual = {p.name.upper(): p.stat().st_size for p in out_dir.rglob("*") if p.is_file()}
    missing = sorted(set(expected) - set(actual))
    mismatched = sorted(n for n in set(expected) & set(actual) if expected[n] != actual[n])
    if missing or mismatched:
        raise SystemExit(
            f"{bin_path.name}: copy in {out_dir} does not match the image -- "
            f"missing {missing or 'none'}, size mismatch {mismatched or 'none'}"
        )
    return expected


def copy_disc(cue_path, out_dir):
    """Copy the whole disc into out_dir from inside DOSBox-X, then verify it."""
    bin_path, _ = parse_cue(cue_path)
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
        "xcopy e:\\*.* f:\\ /s /e /y\n"
        "exit\n",
        encoding="utf-8",
    )
    try:
        subprocess.run(
            [str(DOSBOX), "-silent", "-exit", "-conf", str(conf)],
            check=True,
            timeout=3600,
        )
    finally:
        conf.unlink(missing_ok=True)

    expected = verify_against_image(bin_path, out_dir)
    print(f"{Path(cue_path).name}: copied {len(expected)} files to {out_dir}, matches image")


def report(cue_path, src_dir, json_path):
    """Inventory the copied files and the .cue audio track table into JSON."""
    bin_path, tracks = parse_cue(cue_path)
    verify_against_image(bin_path, src_dir)

    files = []
    for p in sorted(src_dir.iterdir(), key=lambda x: x.name.upper()):
        if not p.is_file():
            continue
        h = hashlib.sha256()
        with open(p, "rb") as fh:
            for chunk in iter(lambda: fh.read(1 << 20), b""):
                h.update(chunk)
        files.append({"name": p.name.upper(), "size": p.stat().st_size, "sha256": h.hexdigest()})

    total_sectors = bin_path.stat().st_size // RAW_SECTOR
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
                "image": bin_path.name,
                "total_raw_sectors": total_sectors,
                "files": files,
                "audio_tracks": audio,
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )
    print(f"{Path(cue_path).name}: {len(files)} files, {len(audio)} audio tracks -> {json_path}")


def main():
    argv = sys.argv[1:]
    if len(argv) == 3 and argv[0] == "copy":
        copy_disc(Path(argv[1]), Path(argv[2]))
    elif len(argv) == 4 and argv[0] == "report":
        report(Path(argv[1]), Path(argv[2]), Path(argv[3]))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
