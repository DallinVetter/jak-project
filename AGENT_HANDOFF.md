# OpenGOAL PS5 agent handoff

## Current state

- Plan/goal: Native OpenGOAL / Jak 1 PS5 bring-up
- Repository: jak-project | feature/ps5-port
- Platform dependency: ../ps5-opengl | feature/sdl3-ps5 | 6305e7f | hardware proven
- Audit and hardware findings: ps5/AUDIT.md. Tooling: ps5/README.md.
- Desktop regression check: Linux gk builds clean with the PS5 changes (build/linux) and runs.
- Game data: extracted from the owner's ISO on the host (out/jak1: iso 1.4 GB, fr3 201 MB,
  KERNEL.CGO/GAME.CGO/GAME.fr3 present). Deployed to /data/opengoal/data (705 files, 1574 MiB,
  read-back verified). Not in Git.
- Candidate: gk with real data and a 1 GiB heap
  - commit 8206be2e5081 | eboot e8f8a63e3bf06330 | ../candidates/og-gk-20261012T224001Z
  - evidence ../ps5-homebrew-dev-protocol/results/PPSA99005-20261012T224002Z
- Hardware result: PASS for the first milestone. gk launched, used /data/opengoal/data as its
  project directory, loaded game data, brought up SDL (ps5 driver) and OpenGL 4.6 Core, ran the
  renderer init (textures and shaders compiled on the GPU), created the 1920x1080 display,
  started all four system threads, and entered the graphics loop. It ran 46 s and the cycle
  closed it cleanly. No new console error record. The EE stopped exactly where designed:
  "EE main memory ... granted protection 0x3 ... is not executable: GOAL code cannot run."
- Console: available; nothing running; /data/opengoal/data populated.
- State: first milestone done; the single diagnostic cycle reached EOF before the helper's
  first reply. Source/artifact inspection identifies missing FW 13.42 support in the helper's
  pinned SDK v0.40 as the leading cause. Hardware is paused pending owner instruction.

## What each run since real data established

- Default 128 MiB heap: gk ran out of memory uploading the common level's textures
  ("Internal Memory is running out"). Fixed with a 1 GiB direct-memory heap (ps5/title).
- ImGui's GL backend opened libGL with dlopen ("Failed to initialize OpenGL loader!"). Fixed by
  building it with IMGUI_IMPL_OPENGL_LOADER_CUSTOM against glad (ps5/cmake/PS5Build.cmake).
- With both fixed, gk reaches and holds the graphics loop. The renderer draws without the EE,
  so the title stays up; the game cannot advance because GOAL code cannot execute.

## Current blocker: elevation helper transport (owner chose option 1 = elevation)

A sandboxed native title gets read/write only from mmap, mprotect, sceKernelMprotect and
flexible memory; sceKernelJitCreateSharedMemory returns EPERM (ps5/AUDIT.md). gk confirms this
at run time: EE main memory maps RW, and the EE stops instead of faulting.

The elevated probe is scoped to this title and the qualified dev environment, and is not wired
into the OpenGOAL runtime. The initial run did not engage elevation. The ONE subsequent
diagnostic cycle, candidate `og-elev-diag-20261013T025138Z` from `071eba480`, established:

- Helper open and sceNetSocket succeeded; options 0x1105, 0x1106 and 0x1109 all succeeded.
- The title connected to 127.0.0.1:9021, streamed all 154456 bytes of /app0/lapy.elf and
  successfully sent the 24-byte request. The first sceNetRecv(requested=24) returned 0 (EOF).
- Client returned Status::transport_error; elevation remained status=9 / path=helper and
  uid/euid=1. A successful send proves local transport acceptance, not helper entry/execution.
  Logged errno=1 on successful operations/EOF is stale, not evidence of an EPERM socket failure.
- Title closed cleanly; elfldr and FTP remained healthy (owner report). No second diagnostic
  hardware run occurred. This does not establish elevated executable-memory policy.
- Evidence: ../ps5-homebrew-dev-protocol/results/PPSA99005-20261013T025138Z
  (data_opengoal_elevation_transport.log, data_opengoal_elevated_probe.log, cycle.log).

Files (committed):

- ps5/probe/elevated_execmem_probe.c: standalone main(); runs the executable-memory checks
  (phase "pre-elevation"), requests the title-scoped elevation once, reruns identical checks
  (phase "post-elevation"), logging return values, errno, the kernel-reported protection and
  whether execute actually works; writes /data/opengoal/elevated_probe.log; exits cleanly.
- ps5/probe/title_elevate.cpp: thin extern-C shim over the documented, already-qualified
  elevation::request(filesystem) client. The title calls no kernel primitive itself; it asks a
  resident helper or the packaged /app0/lapy.elf (sent to elfldr on 9021) to raise its own
  process credentials, and that returns ok only after a /data write/read/list proof.
- ps5/probe/build_elevation.sh: stages the title via stage_title.py, compiling the boilerplate's
  sandbox-elevation client and packaging the pinned exact-title PPSA99005 Lapy helper
  (build-lapy-helper.py, already verified to build in this environment: lapy.elf,
  target_title=PPSA99005, mode=elf-helper). For diagnostic builds, it now patches a private
  elevation.cpp copy with ps5/probe/elevation_transport.patch; the sibling client stays untouched.
- ps5/probe/elevation_transport.patch: diagnostic-only run_helper()/exchange() logging to
  /data/opengoal/elevation_transport.log. Records helper open, socket creation, each socket
  option (0x1105/0x1106/0x1109), connect, read/stream byte counts (including partial sends),
  request send, first receive, prepare reply kind/status, preparation, prepared send, final
  receive/reply, and helper status/close results. Logging saves/restores errno. Socket-option
  failures, short-circuiting, endpoint, protocol validation and return behavior are preserved.
- ps5/stage_title.py: added --src-file (place a source/header under src/) and --add-file (place
  one file in the title folder) used by the above.

## Source-only socket investigation (2026-10-13 UTC)

Inspected the boilerplate client/build scripts, pinned Lapy source and manifests, frozen helper,
SDK v0.40/v0.42 startup sources, protocol scripts/docs and the specified evidence. No existing
local elfldr implementation or deployed loader revision was found in the inspected host trees
or evidence. Public upstream source was downloaded only for inspection; the loader findings
below apply to elfldr commit `02cfe91eb3f9697787460ad77d73ff951f801f50`, not a verified console
binary. SDK sources inspected: v0.40 `13ccc2d5bf2ac396cdf5007c2b72493cb3d5c8bb`,
v0.42 `4eb701204fc3f8d31e84cf8ca272974e2be9c867`.

- Ownership: socksrv.c on_connection() reads the ELF, defaults `pipe` to 1 and passes the
  accepted socket to elfldr_spawn(). elfldr_exec() uses pt_rdup (remote syscall 0x25b) and
  pt_dup2 to install that connection as child fds 0/1/2, then closes the temporary duplicate.
  serve_elfldr() closes its own accepted fd after on_connection() returns. Child references
  keep the same TCP connection alive; the parent's close alone does not imply client EOF.
  There is no separate helper listener/connect or helper TCP fd in payload_args_t. The latter's
  rwpipe/rwpair are SDK kernel-access resources, unrelated to the title's protocol connection.
- Pinned Lapy `54a095c0f19161825e845daa760a03b446e654fa`, source/owned_root_daemon.c:855-952,
  reads request/prepared from STDIN_FILENO (0) and writes prepare/response to STDOUT_FILENO (1).
  It expects loader-installed stdio and exits after one request. ps5log_init_default() uses a
  separate channel and does not capture/replace stdout. Client/helper 24-byte wire ABIs match.
- ELF framing: the frozen helper has e_shoff=153176, e_shnum=20, e_shentsize=64. elfldr_read()
  consumes exactly 154456 bytes, also the maximum non-NOBITS section extent. It needs no
  upload EOF and leaves the following 24 request bytes for the helper. No framing mismatch
  was found for these bytes and the inspected upstream loader.
- EOF before a reply requires the peer's sending side to close with no reply bytes queued
  (FIN/write shutdown or final socket-reference release); it is not a receive timeout. Causes
  include missing/failed stdio handoff followed by parent close, payload startup/early exit or
  termination, and a helper read/write failure followed by exit. Upstream loader read/spawn
  errors normally attempt a text error on the connection, unlike the observed zero-byte reply.
  pt_rdup/pt_dup2 results are unchecked in the inspected loader, so a handoff failure remains
  possible until the actual deployed loader is identified. Port health cannot prove child health.
- Lapy also runs firmware/directory/credential/group/thread/root-layout preflight before
  run_helper_request() (main():986-1049). Any preflight failure goes to done and returns without
  a wire response. An initial request read failure or first reply write failure likewise returns
  without a complete reply. A parsed invalid request/title normally receives an error response.
- Leading cause: build-lapy-helper.py deliberately pins SDK v0.40. Its crt/kernel.c
  __kernel_init() recognizes 13.00/13.20 but not 13.42 (0x13420000); default returns -ENOSYS
  (-78). crt/crt.c payload_init() propagates this; _start() terminates before Lapy main(),
  ps5log or the request read. Child exit releases its stdio references after the loader releases
  its own fd. This explains first-reply EOF while elfldr/FTP stay healthy. It is a concrete
  helper-SDK/firmware incompatibility, not an established elfldr socket-lifecycle mismatch.
  The exact failing console stage remains inferred because no helper-entry/CRT log was captured.
- Artifact checks: frozen lapy.elf SHA-256
  `5c9f8b319054d849a50e9f4eb899bd49b4b89903b1c955bdf821f223cb25b95c` matches the pinned
  PPSA99005 elf-helper manifest. Its SDK crt1.o hash matches the full build manifest, and
  disassembly confirms only 0x13000000/0x13200000 in the final firmware branch with -78 as the
  default return. The source cache is clean. Host client tests mock the loader/SDK boundary;
  successful builds and prior qualification on 6.02/12.70 do not establish 13.42 compatibility.

Source anchors:
[elfldr socket server](https://github.com/ps5-payload-dev/elfldr/blob/02cfe91eb3f9697787460ad77d73ff951f801f50/socksrv.c),
[elfldr child stdio/framing](https://github.com/ps5-payload-dev/elfldr/blob/02cfe91eb3f9697787460ad77d73ff951f801f50/elfldr.c),
[SDK v0.40 firmware gate](https://github.com/ps5-payload-dev/sdk/blob/13ccc2d5bf2ac396cdf5007c2b72493cb3d5c8bb/crt/kernel.c),
[SDK v0.40 startup](https://github.com/ps5-payload-dev/sdk/blob/13ccc2d5bf2ac396cdf5007c2b72493cb3d5c8bb/crt/crt.c),
[SDK v0.42 firmware gate](https://github.com/ps5-payload-dev/sdk/blob/4eb701204fc3f8d31e84cf8ca272974e2be9c867/crt/kernel.c).

## Next action (stopped; awaiting owner direction)

Do NOT run hardware again yet. No protocol, endpoint, validation, helper, SDK or runtime source
was changed during this investigation; only this handoff was updated.

Smallest diagnostic: record the deployed elfldr revision and, if a further cycle is authorized,
use a harmless SDK-v0.42 stdin/stdout echo payload on the existing connection to test fd handoff
without elevation. A diagnostic CRT log must precede/cover __kernel_init() failure; logging only
inside Lapy main() cannot observe the suspected failure. Keep diagnostic bytes off protocol
stdio (stderr aliases the same socket too); use a separate log channel.

Smallest supported compatibility direction: build the helper with a pinned SDK that explicitly
supports 13.42 (v0.42 has that case), adapting Lapy's credential attributes from uint64_t to the
new 32-byte get/set API with checked results and full save/compare/restore. This is not a blind
SDK switch: v0.41+ changed that API. Regenerate and verify build/protocol manifests honestly;
keep all title/PID, layout, preparation and /data proof checks. Do not alias 13.42 to 13.20:
v0.42 has different ALLPROC and ROOTVNODE addresses for 13.40/13.42. SDK coverage permits
startup, not a claim that Lapy's runtime layout validation or elevation will pass.

Stop for owner review before any implementation, next hardware cycle, integration into
runtime.cpp or compiler/JIT redesign. Do not touch ISO or extracted game data.

## Not done yet

- fake_iso lists out/jak1/iso with directory_iterator, which the sandbox refuses (EPERM). Once
  the EE runs, this is the next stop; it needs either elevation (same helper) or a host-made
  file manifest. A rendered frame also needs the EE, so it comes after executable memory.
- Jak 2/3/X kernels compile but lack the System V call-path branches.
- gfx_test.cpp still has the libGL-load pattern (unused by gk).
- Real audio output and controller input (log: "No active game controllers").

## Milestones

- 2026-10-12 | SDL3 | ps5-opengl@6305e7f | PS5 | pass: native SDL3/OpenGL 4.6 hardware gate
- 2026-10-12 | audit | jak-project@ae0f9f04c | host | done: ps5/AUDIT.md
- 2026-10-12 | probe | jak-project@e32eda0a8 | PS5 | partial-pass: no executable memory for a sandboxed title | evidence results/PPSA99005-20261012T205700Z
- 2026-10-12 | gk build | jak-project@94de4eafc | host | pass: native PS5 gk title folder links
- 2026-10-12 | gk launch 2 | jak-project@94de4eafc | PS5 | failed: ghc::filesystem lstat refused under /data | results/PPSA99005-20261012T214014Z
- 2026-10-12 | gk launch 3 | jak-project@612866d28 | PS5 | partial-pass: startup log through GL loader; missing GAME.fr3 | results/PPSA99005-20261012T214750Z
- 2026-10-12 | probe 2 | jak-project@032fa23ac | PS5 | pass: probe completes; EE maps RW, not executable | results/PPSA99005-20261012T215826Z
- 2026-10-12 | data | jak-project@33e0ffa52 | host+PS5 | done: out/jak1 extracted from owner ISO, deployed to /data/opengoal/data
- 2026-10-12 | gk + data | jak-project@5354f31b1 | PS5 | failed: out of memory in texture upload (128 MiB heap) | results/PPSA99005-20261012T223537Z
- 2026-10-12 | gk first milestone | jak-project@8206be2e5 | PS5 | pass: gk loads data, brings up OpenGL 4.6, enters the graphics loop and holds; EE stops at the executable-memory blocker by design | evidence results/PPSA99005-20261012T224002Z | next: elevated executable-memory probe
- 2026-10-12 | elevation probe | jak-project@8350fab12 | host | authored, not run at that point: probe + shim + build script written and syntax-valid; Lapy helper builds for PPSA99005; building/running the probe is refused by the auto-mode permission classifier ("Security Weaken"), recorded then as an assistant-capability limitation; superseded by the owner-reported hardware result above
- 2026-10-12 | elevation transport | hardware result reported by owner: status=9 / helper / transport_error; elfldr reachable on 9021, downloadDataSize=256, title healthy and closed cleanly; diagnostic-only transport logging added and locally checked | next: one explicitly authorized bounded hardware rerun to inspect elevation_transport.log
- 2026-10-13 | elevation diagnostic | 071eba480 | PS5 | transport-failure: first reply EOF | ../ps5-homebrew-dev-protocol/results/PPSA99005-20261013T025138Z | stopped; SDK v0.40 lacks FW 13.42
