#!/usr/bin/env bash
# Build the elevated executable-memory probe as the native title PPSA99005.
#
# A standalone experiment (its own main(), no renderer): it runs the executable-memory checks,
# requests the existing title-scoped elevation, and reruns the same checks. See
# ps5/probe/elevated_execmem_probe.c.
#
# Usage: ps5/probe/build_elevation.sh <new-stage-name>  -> build/ps5/<name>/dist/PPSA99005
#
# Needs ../ps5-opengl (SDK prefix) and the two native-app boilerplates: the GL one that stages
# the title, and the plain one that carries the qualified Lapy elevation client and helper.
# Override with PS5_OPENGL, PS5_GL_SDK, PS5_NATIVE_TEMPLATE, PS5_ELEVATION_TEMPLATE, PS5_LIBC_PRX.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../.." && pwd)
[[ $# -eq 1 && $1 =~ ^[A-Za-z0-9._-]+$ ]] || { sed -n '2,13p' "${BASH_SOURCE[0]}" >&2; exit 2; }
out=$root/build/ps5/$1
[[ ! -e $out ]] || { echo "$out exists: choose a new name" >&2; exit 2; }

opengl=${PS5_OPENGL:-$root/../ps5-opengl}
gl_sdk=${PS5_GL_SDK:-$opengl/build/sdk/ps5-opengl-gl46}
template=${PS5_NATIVE_TEMPLATE:-$root/../ps5-native-app-boilerplate-gl}
elevation=${PS5_ELEVATION_TEMPLATE:-$root/../ps5-native-app-boilerplate}
libc_prx=${PS5_LIBC_PRX:-$elevation/runtime/libc.prx}
client=$elevation/examples/sandbox-elevation
for f in "$client/src/elevation.cpp" "$client/elevation.hpp" "$client/protocol.hpp"; do
    [[ -f $f ]] || { echo "missing elevation client file: $f" >&2; exit 2; }
done

# Build the pinned, exact-title Lapy helper (PPSA99005) that the client sends to the ELF loader.
# build-lapy-helper.py verifies it is the exact-title one-shot helper for this title.
helper=$root/build/ps5/.objects/$1-lapy
rm -rf -- "$helper"
python3 "$elevation/tools/build-lapy-helper.py" PPSA99005 "$helper"
[[ -f $helper/lapy.elf ]] || { echo "lapy helper build produced no lapy.elf" >&2; exit 2; }

libc_args=()
[[ -f $template/runtime/libc.prx ]] || libc_args=(--libc-prx "$libc_prx")
python3 "$root/ps5/stage_title.py" --out "$out" --ps5-opengl "$opengl" --gl-sdk "$gl_sdk" \
    --template "$template" "${libc_args[@]}" \
    --title-name "OpenGOAL PS5 elevated execmem probe" \
    --source "$here/elevated_execmem_probe.c" \
    --source "$here/title_elevate.cpp" \
    --src-file "$client/src/elevation.cpp=sandbox-elevation/src/elevation.cpp" \
    --src-file "$client/elevation.hpp=sandbox-elevation/elevation.hpp" \
    --src-file "$client/protocol.hpp=sandbox-elevation/protocol.hpp" \
    --add-file "$helper/lapy.elf=lapy.elf"
