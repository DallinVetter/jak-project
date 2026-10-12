# OpenGOAL PS5 agent handoff

## Current state

- Plan/goal: Native OpenGOAL / Jak 1 PS5 bring-up
- Repository: jak-project | feature/ps5-port
- Platform dependency: ../ps5-opengl | feature/sdl3-ps5 | 6305e7f | hardware proven
- Graphics: SDL 3.4.10 -> PS5 backend -> EGL -> OpenGL 4.6 Core -> PS5 GPU | PASS, now from gk
- Audit and hardware findings: ps5/AUDIT.md. Tooling: ps5/README.md.
- First milestone: DONE. A native gk title builds, deploys through the hardware cycle,
  launches, and writes a deterministic startup log that names where it stops.
- Candidate: native gk title with the lstat fallback
  - commit 612866d28 | eboot f50609fed04fe589 | ../candidates/og-gk-20261012T214749Z
  - evidence ../ps5-homebrew-dev-protocol/results/PPSA99005-20261012T214750Z
- Hardware result: partial-pass. gk.log shows process entry, project path /app0/data, SDL with
  the ps5 video driver, window, GL context, GL loader (OpenGL 4.6 Core, Mesa 26.2.0, PS5 AGC),
  then: "File /app0/data/out/jak1/fr3/GAME.fr3 cannot be opened: does not exist". The exception
  is caught and logged in main and rethrown, which ends the title. No new console error record.
- Console: available; nothing running; lock released.
- State: blocked on game data (owner) and on the executable-memory decision (owner)

## Blocker 1: game data (needs the owner's own disc)

Renderer initialisation reads out/jak1/fr3/GAME.fr3, and the kernel then needs
out/jak1/iso/KERNEL.CGO and the rest of out/jak1. These are produced on the host from the
owner's Jak and Daxter disc image by this repository's extractor at this commit. None exists
on the build host, and none may be committed.

Needed from the owner: the disc image (or its extracted files) placed under iso_data/jak1 on
the build host. Then: build the desktop tools here (not yet attempted), run the extractor, and
deploy out/jak1 to the console.

Open design point, to settle before deploying data: out/ is several hundred MB and the cycle
re-verifies every file of the title folder on each run. Recommendation: keep the title folder
small and let the PS5 project directory be /data/opengoal/data when it exists (deployed once
over FTP with shaders and assets beside out/), falling back to /app0/data.

## Blocker 2: executable memory (owner decision)

A sandboxed native title cannot get executable memory: mmap, mprotect, sceKernelMprotect and
flexible memory silently grant read/write only; sceKernelJitCreateSharedMemory returns EPERM
(table in ps5/AUDIT.md). GOAL code runs from the EE heap, where code and data share pages. gk
checks what the kernel granted and stops the EE with an error instead of faulting.

1. Elevate the title's process (a helper sent to the local ELF loader, as the boilerplate's
   opt-in Lapy example does for filesystem access). Smallest OpenGOAL change; touches kernel
   credentials or VM-map data on the console; firmware-specific; unproven for execute, so it
   needs its own probe first.
2. Change OpenGOAL so it does not need writable+executable pages. Large compiler and
   kernel-linker work, and still needs some way to load code.

The same elevation would also lift the directory-listing refusal (EPERM in /app0 and /data),
which otherwise needs a host-generated file manifest for fake_iso.

## Next action

1. Owner: provide the disc image; choose the executable-memory approach.
2. With data: build the desktop extractor, produce out/jak1, deploy it, rerun. Expected next
   stops, in order: renderer initialised and "entering the graphics loop"; "EE main memory is
   not executable"; then fake_iso's directory scan once the EE can run.
3. Use `PS5_READY_LOG=/data/opengoal/log/gk.log` with
   `PS5_READY_PATTERN='\[startup\] entering the graphics loop'` once a run reaches that line.

## Not done yet

- Heap: both gk titles ran with ps5-opengl's default 128 MiB heap. A 1 GiB override was
  committed earlier but was never linked in (it sat in an archive); it was removed so the
  lstat fix was the only change. Raise it in ps5/title as its own run if memory runs short.
- Probe leftovers: the 128 MiB EE mapping and commit, and fd 0 with MAP_ANONYMOUS, are still
  unmeasured (the probe died in std::filesystem::current_path; that call is now removed).
- No desktop build has been run; the modified sources pass a Linux syntax check only.
- Jak 2, Jak 3 and Jak X kernels compile but did not get the System V call-path branches.
- gfx_test.cpp has the same libGL load as opengl.cpp and was left alone.
- Real audio output and controller input.

## Milestones

- 2026-10-12 | SDL3 | ps5-opengl@6305e7f | PS5 | pass: native SDL3/OpenGL 4.6 hardware gate | next: OpenGOAL bring-up
- 2026-10-12 | audit | jak-project@ae0f9f04c | host | done: ps5/AUDIT.md | next: executable-memory probe
- 2026-10-12 | probe | jak-project@e32eda0a8 | PS5 | partial-pass: no executable memory for a sandboxed title; /data logging works; directory listing EPERM; current_path crash | evidence results/PPSA99005-20261012T205700Z | next: owner decision, gk host build
- 2026-10-12 | gk build | jak-project@94de4eafc | host | pass: native PS5 gk title folder links | next: first launch
- 2026-10-12 | gk launch 1 | jak-project@94de4eafc | PS5 | no-run: console entered rest mode before the launch was delivered | evidence results/PPSA99005-20261012T213202Z
- 2026-10-12 | gk launch 2 | jak-project@94de4eafc | PS5 | failed: ghc::filesystem lstat refused under /data at log setup | evidence results/PPSA99005-20261012T214014Z | next: lstat fallback
- 2026-10-12 | gk launch 3 | jak-project@612866d28 | PS5 | partial-pass: startup log through GL loader (OpenGL 4.6 Core); stops on missing GAME.fr3 | evidence results/PPSA99005-20261012T214750Z | next: game data, executable-memory decision
