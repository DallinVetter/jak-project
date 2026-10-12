# Jak 1 native PS5 bring-up audit

Audited at jak-project `fb80a68c8` against ps5-opengl `6305e7f` (SDL 3.4.10) and the
native-app boilerplate `4f531c4` that ps5-opengl pins. Nothing below has run on a
console unless it says so.

The PS5 compiler (`clang-18 -target x86_64-sie-ps5`) predefines `__PROSPERO__`,
`__FreeBSD__`, `__unix__`, `__AVX2__`. It does not define `__PS5__`, `__linux__` or
`__APPLE__`. PS5-only code is guarded with `__PROSPERO__`, the macro SDL's PS5 platform
patch also uses.

## 1-3. Build graph: `gk` and `runtime`

`gk` is `game/main.cpp` linked against the static library `runtime` (~350 sources:
kernel, overlord, mips2c, renderer for all four games, plus `glad.c`).

| Target | Links | Needed for a PS5 `gk` |
|---|---|---|
| `runtime` | common fmt SDL3 imgui discord-rpc sound stb_image libco libcurl | all but discord-rpc and libcurl |
| `common` | fmt lzokay replxx zstd tree-sitter sqlite3 libtinyfiledialogs tiny_gltf (draco) | fmt lzokay zstd; sqlite3 only for Jak 2/3 |
| `sound` | fmt cubeb common | fmt common; cubeb replaced (see 8) |

- The top-level `CMakeLists.txt` unconditionally builds goalc, decompiler, lsp, tools,
  tests, curl (OpenSSL), replxx, capstone, zydis and draco. None is needed on the console.
  A PS5 configuration should add only `common`, `game` and their real dependencies.
- `discord-rpc` (4 call sites), `libcurl` (`system/background_worker.cpp`),
  `libtinyfiledialogs` (`common/util/dialogs.h`), `replxx` (`common/repl`) and
  tree-sitter (`common/formatter`) have no PS5 use and want stubs or exclusion.
- `kernel/asm_funcs_x86_64.asm` needs NASM, which is not installed on the build host
  (`third-party/nasm` only carries a Windows package).
- `libco` (IOP kernel coroutines) is plain x86-64 context switching and should build as is.
- The link is not CMake's: the title is linked, converted and signed by the template's
  `tools/build.sh`. `ps5/stage_title.py` drives that for prebuilt objects and archives.
- The native libc lacks the `*_l` locale family, `utimensat` and `catopen`; libc++'s
  locale, iostream and filesystem code needs them. ps5-opengl's
  `native-app/cts_runtime_shims.c` supplies them and is what its CTS runner ships.
  `stage_title.py --cxx-runtime` adds it.

## 4. SDL3

OpenGOAL's vendored `third-party/SDL` has the same content as ps5-opengl's pin. The PS5
build must not compile it: it links ps5-opengl's patched `libSDL3.a` and uses that
snapshot's headers, because `SDL_platform_defines.h` is patched.

The PS5 video driver is fullscreen-only with one window, one unshared context and no
MSAA. `gl_make_display` asks for a resizable high-DPI window and `SDL_GL_DEPTH_SIZE 24`,
`STENCIL 8`, `ALPHA 8`; whether the driver's config choice accepts those is unverified.
`DisplayManager` fullscreen/mode switching and the window icon are not needed.

## 5. OpenGL initialisation

`game/graphics/pipelines/opengl.cpp:239` (and `gfx_test.cpp:44`):

```cpp
gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
if (!gladLoadGL()) { ... return NULL; }
```

`gladLoadGL()` (`third-party/glad/src/glad.c:149`) does `dlopen("libGL.so.1")`. On PS5
that fails, so it returns 0 and display creation aborts even though the first call
already loaded every entry point. Narrowest fix: on PS5 use the first call's result and
skip the second. `glad.c` itself still has to compile and link there (it references
`dlopen`/`dlsym`/`dlclose`).

## 6. Filesystem

- `file_util::get_current_executable_path()` has no branch for this platform (falls off
  the end of a non-void function).
- `setup_project_path` looks for `data/` beside the executable, then for `jak-project` in
  the executable path. `--proj-path` overrides both; on PS5 the default should be a fixed
  directory.
- `get_user_config_dir()` has no PS5 branch (saves, settings, logs under `log/`).
- `fake_iso` enumerates `<project>/out/jak1/iso` with `fs::directory_iterator`. ShadowMount
  Plus lets a sandboxed title open and write files under `/data` but refuses to list
  directories there, so where game data lives depends on what the probe reports for
  `/app0` and `/data`.
- A native title cannot `chdir`; relative paths do not resolve against the title folder.
- `/download0` is a disk image (`download0.dat`) from the FTP side, so it cannot back
  `PS5_READY_LOG`. Logs meant for the hardware cycle go under `/data`.

## 7. Executable memory

`game/runtime.cpp:173` maps EE main memory as one 128 MiB `PROT_READ|WRITE|EXEC`
anonymous private mapping at the hint `0x2123000000`, zeroes it, and `mprotect`s the
first 512 KiB to `PROT_NONE`. DGO loading copies compiled GOAL x86-64 code into it and
the kernel calls into it. Code and data share the heap and are addressed off one base
register, so one mapping must be readable, writable and executable at once. The existing
Apple Silicon split (separate executable view) relies on compiler support that the
x86-64 backend does not have.

Open questions the SDK cannot answer, all covered by the probe in `ps5/probe`:

- whether `mmap`/`mprotect` grant `PROT_EXEC` to a native title at all;
- whether `sceKernelJit*SharedMemory` is available to it, and in which shapes;
- whether 128 MiB can be mapped and committed next to the 128 MiB title heap;
- `runtime.cpp` passes fd `0` with `MAP_ANONYMOUS`; FreeBSD documents `-1`.

`kernel_mprotect` in the payload SDK needs kernel read/write, which an installed title
does not have; ps5-opengl's verifier rejects a title that references it.

## 8. Audio

`snd::Player` (`game/sound/989snd/player.cpp`) opens a cubeb stream (stereo, S16LE,
48 kHz). The mixer's `Tick` runs only from cubeb's callback, so no stream means no sound
engine ticks. cubeb has no PS5 backend. The bring-up path is a PS5-only pacing thread
that calls the same callback at the device rate and discards the PCM.

## 9. Input

`InputManager` calls `SDL_InitSubSystem(SDL_INIT_GAMEPAD)` and only logs on failure, then
reads `game/assets/sdl_controller_db.txt` if present. The PS5 SDL build uses SDL's dummy
joystick backend, so startup does not depend on a pad.

## 10. Packaging and runtime data

At boot `gk` reads from the project directory: `game/graphics/opengl_renderer/shaders/`,
`game/assets/`, `out/jak1/iso/*` (CGO/DGO, VAGs, sound banks, built from the user's own
disc by the host toolchain) and `out/jak1/fr3/`. None of it belongs in Git. The title
folder is uploaded file by file with read-back, so several hundred MB of data should live
outside the candidate that each cycle re-verifies.

Other startup dependencies: the DECI2 listener and `XSocket` (BSD sockets, guarded by
`OS_POSIX`, which this platform does not get), `xdbg` (Linux ptrace; needs a no-op
branch), `common/util/os.cpp` (cpuid, memory size), `SystemThread` and `Timer`.

## Hardware probe results

Probe at `e32eda0a8`, eboot `c3cbc09ed989ce5c`, run once on 2026-10-12 as an installed
title under ShadowMount Plus (uid 1, 16 KiB pages). Evidence:
`../ps5-homebrew-dev-protocol/results/PPSA99005-20261012T205700Z`.

| Question | Result |
|---|---|
| `mmap` anonymous `RWX` | succeeds, but the kernel reports `RW`: execute is dropped silently |
| `mprotect` / `sceKernelMprotect` `RW` to `RWX` | return 0, mapping stays `RW` |
| `mprotect` `RW` to `RX` | returns 0, mapping becomes read-only |
| `sceKernelMapNamedFlexibleMemory` `RWX` | succeeds, reported `RW` |
| `sceKernelJitCreateSharedMemory` | `0x80020001` (EPERM) |
| Log file under `/data` | `mkdir /data/opengoal` and `fopen` for writing both work |
| `std::thread`, mutex, condition variable, `thread_local`, `sleep_for` | work; 16 hardware threads |
| `fs::exists` / `is_directory` on `/app0`, `/data`, `/download0` | work |
| `fs::directory_iterator` on `/app0` and `/data/opengoal` | EPERM on both |
| `fs::current_path` | the title died in this call (`pathconf` is not in the libc or libkernel export lists) |
| Flexible memory available at start | 416 MiB; direct memory 11.4 GiB total |

Not reached because of the `current_path` crash: the 128 MiB EE mapping and commit, RTTI,
and exceptions. The crash left no new console error record, and the SDL3 example behind
the probe did not run.

Consequences:

- **No interface open to a sandboxed title yields executable memory.** A request for it
  never fails, so OpenGOAL must check the reported protection after mapping EE memory
  rather than trust `mmap`'s return value; otherwise the first call into GOAL code faults.
  Running GOAL code needs a decision outside this repository's code: see
  [../AGENT_HANDOFF.md](../AGENT_HANDOFF.md).
- Directory listing is refused even in the title's own folder. `fake_iso`'s scan of
  `out/jak1/iso`, log rotation and every other `directory_iterator` use needs either an
  elevated process or a file manifest generated on the host.
- `fs::current_path`, and anything that reaches it (`fs::absolute` on a relative path),
  must not be called on PS5.

## Minimum change set for a first launch

1. A PS5 CMake configuration that builds only `gk`'s dependency closure with the PS5
   compiler, plus NASM on the host.
2. Platform branches: `OS_POSIX`-style guards, executable path, project and config
   directories, `xdbg` no-op, log file under `/data`.
3. GL loader: skip `gladLoadGL()` on PS5.
4. EE memory: whichever mapping the probe shows works.
5. Null audio output thread in place of cubeb.
6. Stubs for discord-rpc, curl-backed background fetches and dialogs.
7. Stage, freeze and run through `ps5-cycle.sh` with a readiness line in the log.
