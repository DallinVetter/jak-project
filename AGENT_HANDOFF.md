# OpenGOAL PS5 agent handoff

## Current state

- Plan/goal: Native OpenGOAL / Jak 1 PS5 bring-up
- Repository: jak-project | feature/ps5-port
- Platform dependency: ../ps5-opengl | feature/sdl3-ps5 | 6305e7f | hardware proven
- Graphics: SDL 3.4.10 -> PS5 backend -> EGL -> OpenGL 4.6 Core -> PS5 GPU | PASS
- Audit: ps5/AUDIT.md. Tooling: ps5/README.md.
- Build: PASS. `ps5/build_gk.sh <stage>` compiles gk's whole dependency closure with the PS5
  toolchain and links a title folder (eboot 33.6 MB plus shaders and game/assets as /app0/data).
  The touched sources still pass a Linux syntax check; no desktop build was run.
- Candidate: first native gk title
  - commit 94de4eafc | eboot 0351fb1531d48e90 | ../candidates/og-gk-20261012T213201Z
  - installed and settle-verified at /data/homebrew/PPSA99005 (365 files, 0 differ)
  - evidence ../ps5-homebrew-dev-protocol/results/PPSA99005-20261012T213202Z
- Hardware result: no-run. The console began entering rest mode about 13 s before the launch
  was delivered (ShadowMount log: SHELLUI_SHUTDOWN_IN_PROGRESS, MAIN_ON_STANDBY,
  SUSPEND_ON_GOING, no "[GAME] started" line). gk never started; no gk.log; no new error record.
  Nothing about the candidate is known from this run.
- Console: NOT AVAILABLE. FTP (2121) refuses connections since the suspend. No retry was made.
- State: stopped (console unavailable) and blocked-on-decision (executable memory)

## Blocker 1: console in rest mode

The owner has to wake the console and reload its services (ftpsrv, elfldr, ShadowMount Plus).
Unattended cycles send no controller input, so the console's rest-mode timer keeps running;
turning it off in the console's power settings is the owner's call (agents never change
console settings).

## Blocker 2: executable memory (owner decision)

A sandboxed native title cannot get executable memory. mmap, mprotect, sceKernelMprotect and
flexible memory all accept a request for execute and silently grant read/write only;
sceKernelJitCreateSharedMemory returns EPERM (probe evidence
results/PPSA99005-20261012T205700Z, table in ps5/AUDIT.md). OpenGOAL runs compiled GOAL x86-64
code from its EE heap, where code and data share pages, so it needs read+write+execute on one
mapping. gk now checks what the kernel granted and stops the EE with an error instead of
faulting, so startup can still be traced up to that point.

The options differ in kind, not detail:

1. Elevate the title's process so the kernel grants it (a helper sent to the local ELF
   loader, as the boilerplate's opt-in Lapy example does for filesystem access). Smallest
   change to OpenGOAL. Touches kernel credentials or VM-map data on the console, is
   firmware-specific, and whether elevated credentials unlock mmap execute or the JIT
   interface is itself unproven: it needs its own probe.
2. Change OpenGOAL so it does not need writable+executable pages (separate code from data in
   the GOAL compiler and linker, and load code through a path the loader accepts). Large
   compiler and kernel-linker work, and still needs some way to load code.

The same elevation would also lift the directory-listing refusal that blocks fake_iso.

## Next action

1. After the console is back: check `ps5-console.py titles` and `errors 1`, then rerun the same
   frozen candidate unchanged (the cycle re-verifies the install before launching):
   `PS5_KLOG=0 PS5_CLOSE=1 PS5_RUN=30 scripts/ps5-cycle.sh PPSA99005
   ../candidates/og-gk-20261012T213201Z/PPSA99005 /data/opengoal/log/gk.log
   /data/shadowmount/debug.log`
2. Read gk.log: the last `[startup]` line is how far it got. Expected on a clean run: every
   stage through "entering the graphics loop", plus "EE main memory is not executable".
3. Once gk.log is proven, switch the cycle to `PS5_READY_LOG=/data/opengoal/log/gk.log` and
   `PS5_READY_PATTERN='\[startup\] entering the graphics loop'`.
4. Still open from the probe (it died in std::filesystem::current_path before reaching them):
   the 128 MiB EE mapping and commit, RTTI and exceptions on hardware.

## Not done yet

- Game data: nothing from the user's disc is in the title. Where it lives, and how fake_iso
  finds files without listing a directory, is undecided.
- Jak 2, Jak 3 and Jak X kernels compile but did not get the System V call-path branches.
- gfx_test.cpp has the same libGL load as opengl.cpp and was left alone.
- Real audio output and controller input.

## Milestones

- 2026-10-12 | SDL3 | ps5-opengl@6305e7f | PS5 | pass: native SDL3/OpenGL 4.6 hardware gate | next: OpenGOAL bring-up
- 2026-10-12 | audit | jak-project@ae0f9f04c | host | done: ps5/AUDIT.md | next: executable-memory probe
- 2026-10-12 | probe | jak-project@e32eda0a8 | PS5 | partial-pass: no executable memory for a sandboxed title; /data logging works; directory listing EPERM; current_path crash | evidence results/PPSA99005-20261012T205700Z | next: owner decision, gk host build
- 2026-10-12 | gk build | jak-project@94de4eafc | host | pass: native PS5 gk title folder links | next: first launch
- 2026-10-12 | gk first launch | jak-project@94de4eafc | PS5 | no-run: console entered rest mode before the launch was delivered | evidence results/PPSA99005-20261012T213202Z | next: owner wakes console, rerun same candidate
