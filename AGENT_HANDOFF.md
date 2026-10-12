# OpenGOAL PS5 agent handoff

## Current state

- Plan/goal: Native OpenGOAL / Jak 1 PS5 bring-up
- Repository: jak-project | feature/ps5-port
- Platform dependency: ../ps5-opengl | feature/sdl3-ps5 | 6305e7f | hardware proven
- Graphics: SDL 3.4.10 -> PS5 backend -> EGL -> OpenGL 4.6 Core -> PS5 GPU | PASS
- Audit: ps5/AUDIT.md (build graph, GL loader, filesystem, executable memory, audio, input, data)
- Candidate: capability probe only (ps5/probe), no OpenGOAL executable yet
  - commit e32eda0a8 | eboot c3cbc09ed989ce5c | ../candidates/og-probe-20261012T205648Z
  - evidence ../ps5-homebrew-dev-protocol/results/PPSA99005-20261012T205700Z
- Hardware result: partial-pass. Install, settle and launch were clean; the probe logged 28
  lines to /data/opengoal/probe.log, then the title died inside std::filesystem::current_path.
  No new console error record. The console kept answering.
- Target: PS5 native homebrew
- State: blocked-on-decision (executable memory); host build work not blocked

## Blocker: executable memory

A sandboxed native title cannot get executable memory. mmap, mprotect, sceKernelMprotect and
flexible memory all accept a request for execute and silently grant read/write only;
sceKernelJitCreateSharedMemory returns EPERM. OpenGOAL runs compiled GOAL x86-64 code from
its EE heap, where code and data share pages, so it needs read+write+execute on one mapping.

This needs the owner's decision; the options differ in kind, not detail:

1. Elevate the title's process so the kernel grants it (a helper sent to the local ELF
   loader, as the boilerplate's opt-in Lapy example does for filesystem access). Smallest
   change to OpenGOAL. Touches kernel credentials or VM-map data on the console, is
   firmware-specific, and whether elevated credentials unlock mmap execute or the JIT
   interface is itself unproven: it needs its own probe.
2. Change OpenGOAL so it does not need writable+executable pages (separate code from data in
   the GOAL compiler and linker, and load code through a path the loader accepts). Large
   compiler and kernel-linker work, and still needs some way to load code.

The same elevation would also lift the directory-listing refusal (see ps5/AUDIT.md).

## Next action

- Owner: choose the executable-memory approach above.
- Independent of that choice: build gk for PS5 (PS5 CMake configuration, platform branches,
  GL loader guard, null audio, stubs) and get a deterministic startup log up to the point
  where EE memory is mapped. ps5/AUDIT.md lists the minimum change set.
- Before the next probe run: remove the fs::current_path call so the remaining lines (128 MiB
  EE mapping and commit, RTTI, exceptions) are reached. One variable: that call.

## Milestones

- 2026-10-12 | SDL3 | ps5-opengl@6305e7f | PS5 | pass: native SDL3/OpenGL 4.6 hardware gate | next: OpenGOAL bring-up
- 2026-10-12 | audit | jak-project@ae0f9f04c | host | done: ps5/AUDIT.md | next: executable-memory probe
- 2026-10-12 | probe | jak-project@e32eda0a8 | PS5 | partial-pass: no executable memory for a sandboxed title; /data logging works; directory listing EPERM; current_path crash | evidence results/PPSA99005-20261012T205700Z | next: owner decision, gk host build
