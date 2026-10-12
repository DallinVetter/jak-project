# Native PS5 OpenGOAL Port

This branch is reserved for the native PS5 OpenGOAL port.

## Current Status

SDL3 PS5 video support is proven on hardware in `../ps5-opengl`
(`feature/sdl3-ps5`, `6305e7f`): SDL 3.4.10, EGL, OpenGL 4.6 Core.

This branch is now in the native OpenGOAL / Jak 1 bring-up. The first milestone is a
native title that launches through the hardware cycle and logs how far startup gets,
not a playable game.

- [ps5/AUDIT.md](ps5/AUDIT.md): what the port has to change, and the open questions.
- [ps5/README.md](ps5/README.md): host tooling for building title folders.
- [AGENT_HANDOFF.md](AGENT_HANDOFF.md): current state and next action.

## Rules

Keep PS5-specific modifications narrow so future upstream OpenGOAL Jak,
Jak II, and Jak 3 updates can continue to be merged. Guard PS5-only code with
`__PROSPERO__` and leave Linux, macOS and Windows behaviour unchanged.

Use the sibling `../ps5-opengl` SDL3 backend for graphics; do not add another one.

Never commit game data, SDK files, credentials, console addresses or raw evidence.

## Remotes

origin:
  DallinVetter/jak-project

upstream:
  open-goal/jak-project

master should remain close to upstream.

feature/ps5-port contains native PS5-specific development.
