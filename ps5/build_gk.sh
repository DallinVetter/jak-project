#!/usr/bin/env bash
# Compile gk's dependency closure for PS5 and, when a stage name is given, link it
# into a title folder.
#
# Usage: ps5/build_gk.sh                      compile only (build/ps5/cmake-gk)
#        ps5/build_gk.sh <new-stage-name>     also stage build/ps5/<new-stage-name>/dist/<TITLE>
#
# Inputs default to the sibling checkouts; override with PS5_OPENGL, PS5_GL_SDK,
# PS5_SDL3_NATIVE, PS5_NATIVE_TEMPLATE and PS5_LIBC_PRX. BUILD_JOBS limits parallelism.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/.." && pwd)
[[ $# -le 1 ]] || { sed -n '2,9p' "${BASH_SOURCE[0]}" >&2; exit 2; }
stage=${1:-}
[[ -z $stage || $stage =~ ^[A-Za-z0-9._-]+$ ]] || { echo "invalid stage name: $stage" >&2; exit 2; }
[[ -z $stage || ! -e $root/build/ps5/$stage ]] || { echo "build/ps5/$stage exists: choose a new name" >&2; exit 2; }

opengl=${PS5_OPENGL:-$root/../ps5-opengl}
gl_sdk=${PS5_GL_SDK:-$opengl/build/sdk/ps5-opengl-gl46}
sdl=${PS5_SDL3_NATIVE:-$opengl/build/sdl3-native}
template=${PS5_NATIVE_TEMPLATE:-$root/../ps5-native-app-boilerplate-gl}
libc_prx=${PS5_LIBC_PRX:-$root/../ps5-native-app-boilerplate/runtime/libc.prx}
payload_sdk=$template/.deps/native/ps5-payload-sdk
for input in "$sdl/cmake/sdl/libSDL3.a" "$template/tooling/prospero-clang18" "$payload_sdk/target/include"; do
    [[ -e $input ]] || { echo "missing input: $input" >&2; exit 2; }
done

build=$root/build/ps5/cmake-gk
mkdir -p "$build"
launcher=$build/prospero-cc
cat > "$launcher" <<LAUNCHER
#!/bin/sh
# Written by ps5/build_gk.sh: the boilerplate's compiler wrapper with its SDK.
PS5_PAYLOAD_SDK="$payload_sdk" USE_CCACHE="\${USE_CCACHE:-1}" exec sh "$template/tooling/prospero-clang18" "\$@"
LAUNCHER
chmod +x "$launcher"

cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$here/cmake/toolchain.cmake" \
    -DPS5_COMPILER_LAUNCHER="$launcher" -DPS5_SDL3_ROOT="$sdl" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$build" --target ps5_gk --parallel "${BUILD_JOBS:-$(nproc)}"

[[ -n $stage ]] || exit 0
links=()
while IFS= read -r object; do links+=(--link "$object"); done < <(
    find "$build/CMakeFiles/gk_main.dir" -name '*.o' -o -name '*.obj' | sort)
while IFS= read -r archive; do links+=(--link "$archive"); done < <(
    find "$build" -name '*.a' -not -path '*/CMakeFiles/*' | sort)
# Link-time wrappers and overrides of the shims' weak defaults only take effect as objects:
# the linker does not pull an archive member to replace a definition it already has.
sources=()
for source in "$here"/title/*.c; do sources+=(--source "$source"); done
libc_args=()
[[ -f $template/runtime/libc.prx ]] || libc_args=(--libc-prx "$libc_prx")
python3 "$here/stage_title.py" --out "$root/build/ps5/$stage" --ps5-opengl "$opengl" \
    --gl-sdk "$gl_sdk" --template "$template" "${libc_args[@]}" \
    --title-name "OpenGOAL Jak 1 (PS5 bring-up)" --cxx-runtime --wrap lstat \
    --add-dir "$root/game/graphics/opengl_renderer/shaders=data/game/graphics/opengl_renderer/shaders" \
    --add-dir "$root/game/assets=data/game/assets" \
    "${sources[@]}" "${links[@]}" --link "$sdl/cmake/sdl/libSDL3.a"
