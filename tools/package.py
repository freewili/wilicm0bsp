#!/usr/bin/env python3
"""Create a source release including pinned OneWili (unlike GitHub's auto ZIP)."""
import argparse
import io
from pathlib import Path
import subprocess
import tarfile


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args])


def add_repository(archive, root, prefix, revision):
    listing = git(root, "ls-tree", "-rz", "--full-tree", revision)
    for record in listing.split(b"\0"):
        if not record: continue
        metadata, name = record.split(b"\t", 1)
        mode, kind, object_id = metadata.split()
        relative = name.decode("utf-8")
        if kind == b"commit":
            add_repository(archive, root / relative, prefix + "/" + relative, object_id.decode())
            continue
        if kind != b"blob": continue
        if mode == b"120000":
            raise RuntimeError("Source archive requires an explicit symlink policy: " + relative)
        data = git(root, "cat-file", "blob", object_id.decode())
        info = tarfile.TarInfo(prefix + "/" + relative)
        info.size = len(data)
        info.mode = 0o755 if mode == b"100755" else 0o644
        # Zero timestamps prevent clock-skew warnings on offline CM0 images.
        info.mtime = 0
        archive.addfile(info, io.BytesIO(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--ref", default="HEAD", help="Committed BSP revision to package")
    parser.add_argument("--prefix", default="wilicm0bsp")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(args.output, "w:gz", format=tarfile.PAX_FORMAT) as archive:
        add_repository(archive, root, args.prefix, args.ref)
    print(args.output)


if __name__ == "__main__":
    main()
