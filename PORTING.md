# Native PS5 OpenGOAL Port

This branch is reserved for the native PS5 OpenGOAL port.

## Current Status

The active development phase is currently SDL3 PS5 support in:

../ps5-opengl

This repository should remain read-only until the SDL3 PS5 video milestone is
proven on actual hardware.

## Future Role

Once SDL3 PS5 support is working, this branch will contain the minimum
OpenGOAL-specific changes required for native PS5 support.

Keep PS5-specific modifications narrow so future upstream OpenGOAL Jak,
Jak II, and Jak 3 updates can continue to be merged.

## Remotes

origin:
  DallinVetter/jak-project

upstream:
  open-goal/jak-project

master should remain close to upstream.

feature/ps5-port contains native PS5-specific development.
