# PS5 port tooling

Host-side tooling for the native PS5 port. Read [../PORTING.md](../PORTING.md) first;
[AUDIT.md](AUDIT.md) records what the port has to change and why.

Apart from `deploy_data.py`, nothing here talks to a console. Hardware runs go through
`../ps5-homebrew-dev-protocol` (`scripts/freeze-candidate.sh`, then `scripts/ps5-cycle.sh`).

## Requirements

- `../ps5-opengl` on `feature/sdl3-ps5` with its SDK prefix (`build/sdk/ps5-opengl-gl46`)
  and SDL3 native build (`build/sdl3-native`).
- The native-app boilerplate at the revision `../ps5-opengl/dependencies.json` pins, with
  its payload SDK fetched, and a `libc.prx` matching its `runtime/libc.prx.sha256`.
- `clang-18`, `lld-18`, `cmake`, `ninja`, `nasm`, `ccache`, `python3`.

## build_gk.sh

```sh
ps5/build_gk.sh                 # compile gk's dependency closure into build/ps5/cmake-gk
ps5/build_gk.sh gk-001          # also link build/ps5/gk-001/dist/<TITLE>
```

Configures the top-level `CMakeLists.txt` with [cmake/toolchain.cmake](cmake/toolchain.cmake),
which sets `PS5` and makes it include [cmake/PS5Build.cmake](cmake/PS5Build.cmake) instead of
the desktop dependency list: `common`, `game` (the `runtime` library), `sound`, fmt, lzokay,
zstd, stb_image, libco, imgui and sqlite3, compiled with the boilerplate's PS5 compiler wrapper.
SDL is not built here: it is ps5-opengl's patched `libSDL3.a`, with that build's headers overlaid
on the vendored ones. NASM is required for `game/kernel/asm_funcs_x86_64.asm`.

CMake only compiles and archives. `stage_title.py` links the archives and `game/main.cpp`'s
object, and adds the shaders and `game/assets` to the title folder as `/app0/data`, where the
runtime looks for its project directory. Game data from the user's own disc is not added.

## deploy_data.py

On PS5, gk uses `/data/opengoal/data` as its project directory when it exists, and the title's
own `/app0/data` (shaders and assets only) otherwise. `deploy_data.py` sends the shaders,
`game/assets` and `out/<game>/{iso,fr3}` there, each file read back before it is moved into
place, skipping files whose size already matches. The `out/` files are built on the host from
your own disc (`extractor <iso> --extract --decompile --compile`), stay out of Git, and go
only to your console. Because that directory is outside the title folder, a hardware cycle does
not re-verify the game data on every run; rerun the script after changing shaders.

## platform

PS5-only sources linked into the title, so `common/` and `game/` need no branches for them:

- `cubeb_null.cpp`: the cubeb entry points `snd::Player` uses, backed by a thread that pulls PCM
  from the mixer at the stream's rate and discards it. Sound-engine timing is unchanged; nothing
  reaches the audio hardware.
- `unused_services.cpp`: Discord, libcurl and message-box entry points that report "unavailable".
- `replxx_unused.cpp`: line-editor members that only goalc's REPL would call.

## title

Sources linked into the title as objects rather than through an archive. A link-time wrapper
or an override of one of the shims' weak defaults only takes effect that way: the linker does
not pull an archive member to replace a definition it already has, and a system library exports
`__wrap_` names of its own. `stage_title.py` checks after the link that every wrapper is the
title's and records the heap size in `candidate.json`.

- `sandbox_fs.c`: `lstat` answered by `stat` when the sandbox refuses it (linked with
  `--wrap=lstat`). OpenGOAL's `fs::` is ghc::filesystem, which calls `lstat` for every status query.

- `title_heap.c`: a 1 GiB heap from direct memory. With ps5-opengl's default (128 MiB of
  flexible memory) gk runs out of memory while loading the common level's textures.

## Logs

The title writes `/data/opengoal/log/gk.log`, flushed on every line. Lines starting with
`[startup]` mark the stages of startup in order; the last one in a failed run is how far it got.
`/download0` is a disk image from the FTP side and cannot be watched by the hardware cycle.

## stage_title.py

Links prebuilt objects and static archives into a title folder the same way
ps5-opengl's SDL3 lane does, in a new directory under `build/ps5/`. It writes
`candidate.json` with the commit and SHA-256 identities of everything that went in.
`--cxx-runtime` adds the libc++ runtime shims that any code using iostreams, locale or
`std::filesystem` needs.

## probe

A capability probe that runs in front of the hardware-proven SDL3 example and answers the
questions in the audit that the SDK cannot: executable memory, the EE main-memory
mapping, threads, filesystem access and C++ exceptions.

```sh
ps5/probe/build.sh probe-001        # build/ps5/probe-001/dist/PPSA99005
```

It writes one `[probe]` line per fact to `/data/opengoal/probe.log` and to the title's
stdout receipt, and ends with `[probe] complete` before the SDL3 example starts. It never
executes from a page the kernel does not report as executable.
