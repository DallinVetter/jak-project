# PS5 port tooling

Host-side tooling for the native PS5 port. Read [../PORTING.md](../PORTING.md) first;
[AUDIT.md](AUDIT.md) records what the port has to change and why.

Nothing here talks to a console. Hardware runs go through
`../ps5-homebrew-dev-protocol` (`scripts/freeze-candidate.sh`, then `scripts/ps5-cycle.sh`).

## Requirements

- `../ps5-opengl` on `feature/sdl3-ps5` with its SDK prefix (`build/sdk/ps5-opengl-gl46`)
  and SDL3 native build (`build/sdl3-native`).
- The native-app boilerplate at the revision `../ps5-opengl/dependencies.json` pins, with
  its payload SDK fetched, and a `libc.prx` matching its `runtime/libc.prx.sha256`.
- `clang-18`, `lld-18`, `ninja`, `python3`.

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
