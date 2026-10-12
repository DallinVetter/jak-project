#!/usr/bin/env python3
"""Deploy gk's project directory to the console's persistent data folder.

A PS5 gk title uses /data/opengoal/data as its project directory when it exists, and
its own /app0/data otherwise. This sends what gk reads at run time:

  game/graphics/opengl_renderer/shaders, game/assets   from this checkout
  out/<game>/iso, out/<game>/fr3                        built here from your own disc

The out/ files are proprietary game data: they go from this machine to your console and
nowhere else, and are never added to Git.

Every file is sent under a temporary name, read back and compared, then moved into
place. A file whose size already matches is left alone; --verify-all reads those back
too. Nothing is sent while a title is running. The console address comes from PS5_HOST,
through ../ps5-homebrew-dev-protocol/scripts/ps5-console.py.
"""
import argparse
import importlib.util
import io
from pathlib import Path
from posixpath import dirname, join
import sys

ROOT = Path(__file__).resolve().parent.parent
REPO_TREES = ("game/graphics/opengl_renderer/shaders", "game/assets")
GAME_TREES = ("iso", "fr3")


def load_console(protocol):
    path = protocol / "scripts/ps5-console.py"
    spec = importlib.util.spec_from_file_location("ps5_console", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", default="jak1")
    parser.add_argument("--remote", default="/data/opengoal/data")
    parser.add_argument("--protocol", type=Path, default=ROOT.parent / "ps5-homebrew-dev-protocol")
    parser.add_argument("--no-game-data", action="store_true", help="send shaders and assets only")
    parser.add_argument("--verify-all", action="store_true",
                        help="read back files whose size already matches")
    parser.add_argument("--dry-run", action="store_true", help="list what would be sent")
    args = parser.parse_args()
    console = load_console(args.protocol.resolve())

    trees = [ROOT / tree for tree in REPO_TREES]
    if not args.no_game_data:
        trees += [ROOT / "out" / args.game / tree for tree in GAME_TREES]
    files = []
    for tree in trees:
        if not tree.is_dir():
            raise SystemExit(f"missing {tree}: build the game data first")
        files += sorted(item for item in tree.rglob("*") if item.is_file())
    total = sum(item.stat().st_size for item in files)
    print(f"{len(files)} files, {total / (1 << 20):.0f} MiB -> {args.remote}")
    if args.dry_run:
        return

    with console.connect() as ftp:
        titles = console.all_running(ftp)
        if titles:
            raise SystemExit(f"a title is running ({' '.join(titles)}): not replacing its data")
        known = set()
        sent = unchanged = sent_bytes = 0
        for index, item in enumerate(files, 1):
            relative = item.relative_to(ROOT).as_posix()
            remote = join(args.remote, relative)
            size = item.stat().st_size
            if console.remote_size(ftp, remote) == size:
                if not args.verify_all or console.readback(ftp, remote) == item.read_bytes():
                    unchanged += 1
                    continue
            data = item.read_bytes()
            temporary = join(dirname(remote), f".{item.name}.upload")
            console.ensure_directory(ftp, dirname(remote), known)
            console.remove_if_present(ftp, temporary)
            ftp.storbinary(f"STOR {temporary}", io.BytesIO(data), blocksize=console.BLOCK)
            if console.readback(ftp, temporary) != data:
                console.remove_if_present(ftp, temporary)
                raise SystemExit(f"readback of {remote} differs: stopped, nothing moved into place")
            console.remove_if_present(ftp, remote)
            ftp.rename(temporary, remote)
            sent += 1
            sent_bytes += size
            if sent % 50 == 0:
                print(f"  {index}/{len(files)} files, {sent_bytes / (1 << 20):.0f} MiB sent",
                      flush=True)
        mismatched = [item.relative_to(ROOT).as_posix() for item in files
                      if (console.remote_size(ftp, join(args.remote, item.relative_to(ROOT).as_posix()))
                          or 0) != item.stat().st_size]
    print(f"deployed {args.remote}: {sent} sent ({sent_bytes / (1 << 20):.0f} MiB), "
          f"{unchanged} unchanged, {len(files)} files; size mismatches {len(mismatched)}")
    for relative in mismatched[:10]:
        print(f"  mismatch: {relative}")
    if mismatched:
        raise SystemExit(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(130)
