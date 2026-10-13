#!/usr/bin/env python3
"""Host-only builds: pinned SDK v0.42 Lapy helper and harmless elfldr stdio ELF.

Usage: python3 ps5/probe/build_fw1342.py all build/ps5/fw1342-001
No deployment, candidate freezing, or hardware calls are performed.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CACHE = ROOT / "build/ps5/.deps"
SDK_REVISION = "4eb701204fc3f8d31e84cf8ca272974e2be9c867"
SDK_SHA256 = "8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da"
SDK_URL = "https://github.com/ps5-payload-dev/sdk/releases/download/v0.42/ps5-payload-sdk.zip"
SDK = CACHE / "ps5-payload-sdk-v0.42"
SDK_SOURCE = CACHE / ("sdk-source-" + SDK_REVISION)
LAPY_REVISION = "54a095c0f19161825e845daa760a03b446e654fa"
PROTOCOL_SHA256 = "bb02c4aa814eaba7a7a423a31b29ff41f786212c2953678cf29434e85fa0f869"
PATCH = HERE / "lapy_sdk042.patch"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def bootstrap(template):
    spec = importlib.util.spec_from_file_location("lapy_inputs", template / "tools/build-lapy-helper.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if module.LAPY_COMMIT != LAPY_REVISION or module.PROTOCOL_SHA256 != PROTOCOL_SHA256:
        raise RuntimeError("boilerplate Lapy/protocol pin changed; review required")
    module.fetch_lapy()
    module.fetch_ps5log()
    return module


def prepare_sdk(inputs, template):
    CACHE.mkdir(parents=True, exist_ok=True)
    archive = CACHE / "ps5-payload-sdk-v0.42.zip"
    cached = template / ".deps/native/ps5-payload-sdk.zip"
    if not archive.exists() and cached.is_file() and digest(cached) == SDK_SHA256:
        shutil.copy2(cached, archive)
    inputs.download(SDK_URL, archive, SDK_SHA256)
    if not SDK.exists():
        with zipfile.ZipFile(archive) as source:
            for member in source.infolist():
                parts = Path(member.filename).parts
                if not parts or parts[0] != "ps5-payload-sdk" or ".." in parts:
                    raise RuntimeError(f"unexpected SDK archive path: {member.filename}")
                target = SDK.joinpath(*parts[1:])
                if member.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(source.read(member))
                    mode = member.external_attr >> 16
                    if mode:
                        target.chmod(mode)
    # A stamp alone would not detect a modified installed CRT/header/compiler wrapper.
    with zipfile.ZipFile(archive) as source:
        for member in source.infolist():
            if member.is_dir():
                continue
            relative = Path(member.filename).relative_to("ps5-payload-sdk")
            if digest(SDK / relative) != hashlib.sha256(source.read(member)).hexdigest():
                raise RuntimeError(f"installed SDK differs from pinned archive: {relative}")
    if not SDK_SOURCE.exists():
        SDK_SOURCE.mkdir()
        run("git", "init", "-q", str(SDK_SOURCE))
        run("git", "-C", str(SDK_SOURCE), "fetch", "-q", "--depth", "1",
            "https://github.com/ps5-payload-dev/sdk.git", SDK_REVISION)
        run("git", "-C", str(SDK_SOURCE), "-c", "advice.detachedHead=false",
            "checkout", "-q", "--detach", "FETCH_HEAD")
    head = subprocess.check_output(["git", "-C", str(SDK_SOURCE), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(["git", "-C", str(SDK_SOURCE), "status", "--porcelain"], text=True)
    if head != SDK_REVISION or dirty:
        raise RuntimeError("SDK source must match the clean pinned revision")
    if "case 0x13420000:" not in (SDK_SOURCE / "crt/kernel.c").read_text():
        raise RuntimeError("SDK lacks an explicit FW 13.42 case")


def sdk_manifest():
    return {"sdk_version": "v0.42", "sdk_revision": SDK_REVISION,
            "sdk_archive_sha256": SDK_SHA256,
            "sdk_crt_sha256": digest(SDK / "target/lib/crt1.o"),
            "sdk_kernel_source_sha256": digest(SDK_SOURCE / "crt/kernel.c"),
            "compiler_version": subprocess.check_output(
                [str(SDK / "bin/prospero-clang"), "--version"], text=True).splitlines()[0],
            "console_validated": False, "validated_firmware": []}


def build_helper(inputs, output):
    output.mkdir(parents=True, exist_ok=False)
    private = output / "source"
    private.mkdir()
    # Copy only tracked files from the verified clean pin, excluding old build outputs.
    files = subprocess.check_output(["git", "-C", str(inputs.LAPY), "ls-files"], text=True).splitlines()
    for name in files:
        target = private / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(inputs.LAPY / name, target)
    with PATCH.open("rb") as patch:
        run("patch", "--batch", "--forward", "--fuzz=0", "-p1", cwd=private, stdin=patch)
    changed = [name for name in files if digest(private / name) != digest(inputs.LAPY / name)]
    if changed != ["source/owned_root_daemon.c"]:
        raise RuntimeError(f"unexpected patched files: {changed}")
    if digest(private / "source/lapy_elevation_protocol.h") != PROTOCOL_SHA256:
        raise RuntimeError("helper protocol changed")
    environment = os.environ.copy()
    environment.update(PS5_PAYLOAD_SDK=str(SDK), LOGGING_CLIENT=str(inputs.PS5LOG))
    run("make", "owned-helper", "TARGET_TITLE=PPSA99005", cwd=private, env=environment)
    built = private / "build/owned_root_helper-PPSA99005"
    manifest = json.loads((built / "lapy-manifest.json").read_text())
    expected = {"schema": "lapy-owned-build/1", "target_title": "PPSA99005",
                "mode": "elf-helper", "max_requests": 1, "service": False,
                "require_client_result": False, "console_validated": False,
                "protocol_sha256": PROTOCOL_SHA256,
                "features": ["root_layout_probe_retry"], "elf_sha256": digest(built / "lapy.elf")}
    if any(manifest.get(key) != value for key, value in expected.items()):
        raise RuntimeError("helper manifest verification failed")
    if (built / "lapy.elf").read_bytes()[:6] != b"\x7fELF\x02\x01":
        raise RuntimeError("helper must be little-endian ELF64")
    metadata = {**sdk_manifest(), "upstream_lapy_revision": LAPY_REVISION,
                "compatibility_patch_sha256": digest(PATCH), "credential_attrs_bytes": 32,
                "ptrace_attr_byte": 3, "builder_sha256": digest(Path(__file__))}
    manifest.update(metadata)
    full = json.loads((built / "manifest.json").read_text())
    full.update(metadata)
    (output / "lapy-manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    (output / "manifest.json").write_text(json.dumps(full, indent=2, sort_keys=True) + "\n")
    shutil.copy2(built / "lapy.elf", output / "lapy.elf")
    shutil.copy2(private / "LICENSE", output / "Lapy-MIT.txt")
    print(f"Helper: {output / 'lapy.elf'}\nbuild_id={manifest['build_id']}\nsha256={manifest['elf_sha256']}")


def build_stdio(output):
    output.mkdir(parents=True, exist_ok=False)
    sources = [HERE / "elfldr_stdio.c", HERE / "elfldr_stdio_crt.c",
               SDK_SOURCE / "crt/syscall.c", SDK_SOURCE / "crt/_start.S"]
    identity = {"sources_sha256": {p.name: digest(p) for p in sources},
                "sdk": sdk_manifest(), "builder_sha256": digest(Path(__file__)),
                "startup": "minimal-read-only-entry-and-syscall-bootstrap",
                "request_hex": b"OGSTDIO-REQ-v1!\n".hex(),
                "reply_hex": b"OGSTDIO-ACK-v1!\n".hex()}
    build_id = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
    (output / "stdio_identity.h").write_text(
        f'#define STDIO_BUILD_ID "{build_id}"\n#define STDIO_SDK_REVISION "{SDK_REVISION}"\n')
    elf = output / "elfldr-stdio.elf"
    command = [str(SDK / "bin/prospero-clang"), "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-ffreestanding", "-fno-builtin", "-nostartfiles", "-nodefaultlibs",
               "-Wno-unused-command-line-argument", "-fPIC", "-fomit-frame-pointer",
               "-I" + str(SDK_SOURCE / "crt"), "-I" + str(output),
               *map(str, sources), "-o", str(elf)]
    with (output / "build.log").open("w") as log:
        log.write(json.dumps(command) + "\n")
        log.flush()
        run(*command, stdout=log, stderr=subprocess.STDOUT)
    # No stock CRT, imports, credential writes or mprotect code may sneak in.
    symbols = subprocess.check_output(["nm", str(elf)], text=True)
    forbidden = ("__patch_init", "__kernel_init", "kernel_copyin", "kernel_copyout",
                 "kernel_set_", "kernel_mprotect", "mprotect", "__rtld_", "sceKernelJit")
    if any(name in symbols for name in forbidden) or subprocess.check_output(["nm", "-u", str(elf)]):
        raise RuntimeError("diagnostic contains unexpected runtime/kernel dependencies")
    manifest = {"schema": "opengoal-elfldr-stdio/1", "build_id": build_id,
                **identity, **sdk_manifest(), "elf_sha256": digest(elf),
                "firmware_gate": "13.42", "io_deadline_ms": 5000,
                "log_path": "/data/opengoal/elfldr_stdio.log",
                "stock_crt_initialization_tested": False}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    shutil.copy2(SDK_SOURCE / "LICENSE", output / "SDK-LICENSE")
    print(f"Diagnostic: {elf}\nbuild_id={build_id}\nsha256={manifest['elf_sha256']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("all", "helper", "stdio"))
    parser.add_argument("output", type=Path)
    parser.add_argument("--template", type=Path,
                        default=Path(os.environ.get("PS5_ELEVATION_TEMPLATE", ROOT.parent / "ps5-native-app-boilerplate")))
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error("output already exists; use a new host build directory")
    inputs = bootstrap(args.template.resolve())
    prepare_sdk(inputs, args.template.resolve())
    if args.mode == "all":
        output.mkdir(parents=True)
        build_stdio(output / "stdio")
        build_helper(inputs, output / "helper")
    elif args.mode == "helper":
        build_helper(inputs, output)
    else:
        build_stdio(output)


if __name__ == "__main__":
    main()
