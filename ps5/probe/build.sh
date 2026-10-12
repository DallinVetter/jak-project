#!/usr/bin/env bash
# Build the capability probe as the native title PPSA99005: the hardware-proven
# SDL3 example from ../ps5-opengl with the probe run in front of its main().
#
# Usage: ps5/probe/build.sh <new-stage-name>      -> build/ps5/<new-stage-name>/dist/PPSA99005
#
# Needs ../ps5-opengl with its SDK prefix and SDL3 native build, and the native-app
# template at the revision ps5-opengl pins. Override the defaults with PS5_OPENGL,
# PS5_GL_SDK, PS5_SDL3_NATIVE, PS5_NATIVE_TEMPLATE and PS5_LIBC_PRX.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../.." && pwd)
[[ $# -eq 1 && $1 =~ ^[A-Za-z0-9._-]+$ ]] || { sed -n '2,9p' "${BASH_SOURCE[0]}" >&2; exit 2; }
out=$root/build/ps5/$1
[[ ! -e $out ]] || { echo "$out exists: choose a new name" >&2; exit 2; }

opengl=${PS5_OPENGL:-$root/../ps5-opengl}
gl_sdk=${PS5_GL_SDK:-$opengl/build/sdk/ps5-opengl-gl46}
sdl=${PS5_SDL3_NATIVE:-$opengl/build/sdl3-native}
template=${PS5_NATIVE_TEMPLATE:-$root/../ps5-native-app-boilerplate-gl}
libc_prx=${PS5_LIBC_PRX:-$root/../ps5-native-app-boilerplate/runtime/libc.prx}

objects=$root/build/ps5/.objects/$1
mkdir -p "$objects"
# OpenGOAL's own sources need exceptions and RTTI, which the template's compile
# step turns off, so this object is built here and handed to the link.
PS5_PAYLOAD_SDK=$template/.deps/native/ps5-payload-sdk sh "$template/tooling/prospero-clang18" \
    -std=c++20 -O2 -Wall -Wextra -fexceptions -frtti -fPIC -ffunction-sections -fdata-sections \
    -c "$here/runtime_probe.cpp" -o "$objects/runtime_probe.o"

libc_args=()
[[ -f $template/runtime/libc.prx ]] || libc_args=(--libc-prx "$libc_prx")
python3 "$root/ps5/stage_title.py" --out "$out" --ps5-opengl "$opengl" --gl-sdk "$gl_sdk" \
    --template "$template" "${libc_args[@]}" --title-name "OpenGOAL PS5 capability probe" \
    --source "$here/execmem_probe.c" --wrap main --cxx-runtime \
    --link "$objects/runtime_probe.o" \
    --link "$sdl/cmake/sdl/libSDL3.a" \
    --link "$sdl/cmake/CMakeFiles/sdl3-ps5-example.dir/example.c.obj"
