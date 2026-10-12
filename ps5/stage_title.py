#!/usr/bin/env python3
"""Stage and link a native PS5 title folder from prebuilt objects and archives.

Follows ../ps5-opengl/integration/SDL3/folder.py, the lane that produced the
hardware-proven SDL3 title: a private copy of the pinned native-app template,
the unchanged ps5-opengl native-app shims, heap wrappers and linker script, and
the template's own tools/build.sh for the link, conversion and FSELF.

Nothing here talks to a console.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "ps5"
WRAPPED_HEAP = ("malloc", "calloc", "realloc", "free", "posix_memalign", "malloc_usable_size")
# ps5-opengl's CTS runner shims: the locale, thread-exit and filesystem symbols
# libc++ needs and the native libc lacks. Its fopen wrapper resolves relative
# paths against /app0, since a native title cannot chdir.
CXX_RUNTIME_SHIMS = "cts_runtime_shims.c"
CXX_RUNTIME_WRAPS = ("fopen",)


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def symbols(elf, *flags):
    """{name: (kind, address)} from llvm-nm."""
    out = run("llvm-nm-18", *flags, elf, capture_output=True, text=True).stdout
    result = {}
    for line in out.splitlines():
        parts = line.split()
        result[parts[-1]] = (parts[-2], int(parts[0], 16) if len(parts) == 3 else None)
    return result


def read_u64(elf, address):
    data = Path(elf).read_bytes()
    offset, = struct.unpack_from("<Q", data, 32)
    size, count = struct.unpack_from("<HH", data, 54)
    for index in range(count):
        kind, _, file_offset, start, _, file_size, _, _ = struct.unpack_from(
            "<IIQQQQQQ", data, offset + index * size)
        if kind == 1 and start <= address < start + file_size:
            return struct.unpack_from("<Q", data, file_offset + address - start)[0]
    raise SystemExit(f"address 0x{address:x} is not in a loaded segment of {elf}")


def verify_link(elf, wraps):
    """What the link really did: a wrapper or override that lives in an archive is silently
    not used, and a system library exports some __wrap_ names of its own."""
    table = symbols(elf)
    for name in wraps:
        if table.get(f"__wrap_{name}", ("U",))[0] not in "Tt":
            raise SystemExit(f"--wrap={name}: the title does not define __wrap_{name}")
    stray = [name for name, (kind, _) in table.items()
             if kind == "U" and name.startswith(("__wrap_", "__real_"))]
    if stray:
        raise SystemExit(f"unresolved wrapper symbols: {stray}")
    return read_u64(elf, table["ps5_opengl_heap_size"][1])


def replace_once(path, old, new):
    text = path.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"Native template contract changed: {path}: {old}")
    path.write_text(text.replace(old, new))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path, help="new directory below build/ps5/")
    parser.add_argument("--ps5-opengl", required=True, type=Path, help="ps5-opengl checkout")
    parser.add_argument("--gl-sdk", required=True, type=Path, help="ps5-opengl SDK prefix")
    parser.add_argument("--template", required=True, type=Path,
                        help="native-app boilerplate at the revision ps5-opengl pins")
    parser.add_argument("--libc-prx", type=Path,
                        help="runtime libc.prx matching the template's libc.prx.sha256")
    parser.add_argument("--title-name", required=True)
    parser.add_argument("--source", action="append", default=[], type=Path,
                        help="C/C++ source compiled by the template (no exceptions or RTTI)")
    parser.add_argument("--src-file", action="append", default=[], metavar="SRC=DEST",
                        help="copy a file into src/DEST (DEST may name subdirs); a .c/.cpp there "
                             "is compiled, a header is only placed for #include resolution")
    parser.add_argument("--link", action="append", default=[], type=Path,
                        help="object or static archive, in link order")
    parser.add_argument("--wrap", action="append", default=[],
                        help="extra symbol to link with --wrap")
    parser.add_argument("--add-dir", action="append", default=[], metavar="SOURCE=DEST",
                        help="copy a directory into the title folder (seen as /app0/DEST)")
    parser.add_argument("--add-file", action="append", default=[], metavar="SOURCE=DEST",
                        help="copy a file into the title folder at DEST (seen as /app0/DEST)")
    parser.add_argument("--cxx-runtime", action="store_true",
                        help="add the libc++ runtime shims (needed by iostreams, locale, "
                             "std::filesystem and thread_local destructors)")
    args = parser.parse_args()
    out, opengl, prefix, template = (p.resolve() for p in
                                     (args.out, args.ps5_opengl, args.gl_sdk, args.template))
    if not out.is_relative_to(BUILD) or out == BUILD or out.exists():
        parser.error(f"--out must be a new directory below {BUILD}")

    pinned = json.loads((opengl / "dependencies.json").read_text())["native_boilerplate"]["revision"]
    template_commit = run("git", "-c", f"safe.directory={template}", "-C", template, "rev-parse",
                          "HEAD", capture_output=True, text=True).stdout.strip()
    if template_commit != pinned:
        raise SystemExit(f"Template is {template_commit}; ps5-opengl pins {pinned}")
    opengl_commit = run("git", "-C", opengl, "rev-parse", "HEAD", capture_output=True,
                        text=True).stdout.strip()
    links = [p.resolve() for p in args.link]
    sources = [p.resolve() for p in args.source]
    for path in (*links, *sources):
        if not path.is_file():
            raise SystemExit(f"Missing input: {path}")
    inputs = {str(p): digest(p) for p in (*links, *sources)}
    added = []
    for item in args.add_dir:
        source, separator, dest = item.partition("=")
        if not separator or not dest or Path(dest).is_absolute() or ".." in Path(dest).parts:
            parser.error(f"--add-dir wants SOURCE=DEST with a relative DEST: {item}")
        if not Path(source).is_dir():
            raise SystemExit(f"Missing directory: {source}")
        added.append((Path(source).resolve(), dest, "dir"))
    for item in args.add_file:
        source, separator, dest = item.partition("=")
        if not separator or not dest or Path(dest).is_absolute() or ".." in Path(dest).parts:
            parser.error(f"--add-file wants SOURCE=DEST with a relative DEST: {item}")
        if not Path(source).is_file():
            raise SystemExit(f"Missing file: {source}")
        added.append((Path(source).resolve(), dest, "file"))

    out.mkdir(parents=True)
    # Copy every potentially written dependency. No symlinks into canonical caches.
    for directory in ("runtime", "sce_sys", "tooling", "tools", ".deps/native"):
        shutil.copytree(template / directory, out / directory)
    if args.libc_prx:
        if (out / "runtime/libc.prx").exists():
            raise SystemExit("The template already has a runtime libc.prx; omit --libc-prx")
        shutil.copy2(args.libc_prx.resolve(), out / "runtime/libc.prx")
    # The template pins the only accepted runtime libc; nothing is rebuilt here.
    run("sha256sum", "--check", "--strict", "libc.prx.sha256", cwd=out / "runtime")
    sdk = out / ".deps/native/ps5-payload-sdk"
    for directory in ("src", "vendor"):
        (out / directory).mkdir(parents=True)
    shims = ["runtime_shims.c", "app_heap.c"]
    wraps = [*WRAPPED_HEAP, *args.wrap]
    if args.cxx_runtime:
        shims.append(CXX_RUNTIME_SHIMS)
        wraps += CXX_RUNTIME_WRAPS
    for name in shims:
        shutil.copy2(opengl / "native-app" / name, out / "src" / name)
    for path in sources:
        shutil.copy2(path, out / "src" / path.name)
    for item in args.src_file:
        source, separator, dest = item.partition("=")
        rel = Path(dest)
        if not separator or not dest or rel.is_absolute() or ".." in rel.parts:
            parser.error(f"--src-file wants SRC=DEST with a relative DEST under src/: {item}")
        if not Path(source).is_file():
            raise SystemExit(f"Missing file: {source}")
        target = out / "src" / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(Path(source).resolve(), target)
    shutil.copy2(out / "tooling/native/ps5-pie.ld", out / "tooling/native/ps5-pie-base.ld")
    for name in ("ps5-pie.ld", "app-symbols.map"):
        shutil.copy2(opengl / "native-app" / name, out / "tooling/native" / name)

    param = json.loads((opengl / "native-app/param.json").read_text())
    language = param["localizedParameters"]["defaultLanguage"]
    param["localizedParameters"][language]["titleName"] = args.title_name
    (out / "sce_sys/param.json").write_text(json.dumps(param, indent=2) + "\n")

    replace_once(out / "tooling/native/sce_module_writer.cpp",
                 "write_u64(result.data, result.heap_size, std::numeric_limits<std::uint64_t>::max());",
                 "write_u64(result.data, result.heap_size, 0x10000000ULL);")
    script = out / "tools/build.sh"
    replace_once(script, 'bash "$root/tools/setup-native-dependencies.sh" >/dev/null',
                 'test -x "$root/.deps/native/ps5-payload-sdk/bin/prospero-lld"')
    replace_once(script, '[[ -f $root/runtime/libc.prx ]] || bash "$root/tools/rebuild-libc.sh"',
                 'test -f "$root/runtime/libc.prx"')
    replace_once(script, "--eh-frame-hdr \\",
                 "--eh-frame-hdr " + " ".join(f"--wrap={s}" for s in wraps) + " \\")

    env = os.environ.copy()
    env["PS5_PAYLOAD_SDK"] = str(sdk)
    for name in ("libSceAgc.so", "libSceAgcDriver.so"):
        shutil.copy2(prefix / "lib" / name, sdk / "target/lib" / name)
    compiler_rt = Path(run("clang-18", "--print-resource-dir", capture_output=True,
                           text=True).stdout.strip()) / "lib/linux/libclang_rt.builtins-x86_64.a"
    libraries = [*links, prefix / "lib/libPS5OpenGLCore33.a"]
    libraries += [sdk / f"target/lib/{name}" for name in ("libunwind.a", "libc++abi.a", "libc++.a")]
    libraries += [compiler_rt]
    for path in libraries:
        if not path.is_file():
            raise SystemExit(f"Missing link input: {path}")
    group = (f'SEARCH_DIR("{sdk}/target/lib")\nSEARCH_DIR("{prefix}/lib")\n'
             'EXTERN(ps5_agc_gate2_run)\nGROUP (\n' +
             "".join(f'  "{p}"\n' for p in libraries) + ')\n')
    (out / "vendor/libtitle.a").write_text(group)
    env["APP_STATIC_ARCHIVES"] = "vendor/libtitle.a"
    for key in ("APP_DEFINITIONS", "APP_INCLUDE_PATHS", "PACBREW_PACKAGES", "PACBREW_INCLUDE_PATHS",
                "PACBREW_STATIC_ARCHIVES", "APP_RUNTIME_MODULES", "APP_ROOT_FILES", "BUILD_LABEL"):
        env[key] = ""
    env["APP_ASSETS"] = ""
    run("bash", script, "Folder", env=env, cwd=out)

    heap_size = verify_link(out / "build/llvm-pie.elf", wraps)
    print(f"title heap: {heap_size >> 20} MiB")
    candidate = out / "dist" / param["titleId"]
    # After the template's build: it recreates the title folder. Its ZIP lacks these.
    for source, dest, kind in added:
        target = candidate / dest
        if kind == "dir":
            shutil.copytree(source, target)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    result = {"hardware_run": False,
              "jak_project_commit": run("git", "-C", ROOT, "rev-parse", "HEAD", capture_output=True,
                                        text=True).stdout.strip(),
              "ps5_opengl_commit": opengl_commit,
              "template_commit": template_commit,
              "gl_sdk_manifest_sha256": digest(prefix / "manifest.sha256"),
              "template_libc_sha256": digest(out / "runtime/libc.prx"),
              "heap_size": heap_size,
              "wrapped": wraps,
              "inputs": inputs,
              "files": {str(p.relative_to(candidate)): digest(p)
                        for p in sorted(candidate.rglob("*")) if p.is_file()}}
    (out / "candidate.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"Unlaunched title folder: {candidate}; identities: {out / 'candidate.json'}")


if __name__ == "__main__":
    main()
