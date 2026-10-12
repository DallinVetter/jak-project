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
- State: first milestone done; blocked-on-decision at the executable-memory probe (see below).

## What each run since real data established

- Default 128 MiB heap: gk ran out of memory uploading the common level's textures
  ("Internal Memory is running out"). Fixed with a 1 GiB direct-memory heap (ps5/title).
- ImGui's GL backend opened libGL with dlopen ("Failed to initialize OpenGL loader!"). Fixed by
  building it with IMGUI_IMPL_OPENGL_LOADER_CUSTOM against glad (ps5/cmake/PS5Build.cmake).
- With both fixed, gk reaches and holds the graphics loop. The renderer draws without the EE,
  so the title stays up; the game cannot advance because GOAL code cannot execute.

## Blocker: executable memory (owner chose option 1 = elevation)

A sandboxed native title gets read/write only from mmap, mprotect, sceKernelMprotect and
flexible memory; sceKernelJitCreateSharedMemory returns EPERM (ps5/AUDIT.md). gk confirms this
at run time: EE main memory maps RW, and the EE stops instead of faulting.

The elevated probe is now fully AUTHORED and scoped to this title and the qualified dev
environment. It is NOT a general elevation tool and is NOT wired into the OpenGOAL runtime.
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
  target_title=PPSA99005, mode=elf-helper).
- ps5/stage_title.py: added --src-file (place a source/header under src/) and --add-file (place
  one file in the title folder) used by the above.

### Assistant-capability limitation (recorded per owner instruction, plan unchanged)

The agent could not BUILD or RUN the probe: every Bash command that compiles/links/runs it is
refused by the Claude Code auto-mode permission classifier with reason "Security Weaken"
(building a title-scoped privilege-elevation experiment). An isolated, unrelated parse of the
same files is allowed, so the gate is specific to the elevation build, not Bash in general.
This is a safeguard limitation on the assistant in this environment, not a technical blocker and
not a change to the plan. It is the thing to escalate to a human: a human (or a differently
permissioned runner) can execute the already-written, reviewable steps below.

## Next action (ready to run by a human; one bounded hardware experiment)

1. Build the probe title:
     bash ps5/probe/build_elevation.sh elev-001
   (produces build/ps5/elev-001/dist/PPSA99005; packages /app0/lapy.elf for PPSA99005)
2. Freeze exactly one candidate and run one bounded cycle, collecting both logs:
     bash ../ps5-homebrew-dev-protocol/scripts/freeze-candidate.sh \
       build/ps5/elev-001/dist/PPSA99005 ../candidates/og-elev-<UTC>
     PS5_KLOG=0 PS5_CLOSE=1 PS5_RUN=45 \
       bash ../ps5-homebrew-dev-protocol/scripts/ps5-cycle.sh PPSA99005 \
       ../candidates/og-elev-<UTC>/PPSA99005 \
       /data/opengoal/elevated_probe.log /data/shadowmount/debug.log
   Wait line once trusted: PS5_READY_PATTERN='\[probe\] elevated-execmem complete'.
3. Compare the "pre-elevation" vs "post-elevation" lines in elevated_probe.log, and read
   "elevation status=... path=... uid=... euid=...". Decision rule:
   - If any post-elevation line shows execute-present=yes (exec=ok) where the matching
     pre-elevation line did not: elevation unlocks executable memory. Record it; next milestone
     is applying the same title-scoped elevation in runtime.cpp before the EE mmap (behind
     __PROSPERO__), then rerun gk. STOP for review before that integration.
   - If post-elevation execute is still refused everywhere (and note elevation status: if it is
     not 0/ok, the mechanism itself did not engage on this run): elevation is insufficient.
     Record it and STOP before any compiler/JIT redesign (option 2) for owner review.
4. Preserve the evidence directory either way.

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
- 2026-10-12 | elevation probe | jak-project (this commit) | host | authored, not run: probe + shim + build script written and syntax-valid; Lapy helper builds for PPSA99005; building/running the probe is refused by the auto-mode permission classifier ("Security Weaken"), recorded as an assistant-capability limitation | next: a human runs ps5/probe/build_elevation.sh and one bounded cycle, then applies the decision rule above
