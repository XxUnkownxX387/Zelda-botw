#!/bin/bash
# check.sh - syntax check for C / C++ / ObjC++ sources with the Android NDK clang.
# Usage: ./check.sh [files...]      (no args: the files modified vs HEAD / --cached)
# - toolchain: ANDROID_NDK_HOME (or ANDROID_NDK, default /opt/android-ndk) -> the NDK clang; LARECOMP_CLANG overrides.
# - flags: compile_commands.json when one is found (LARECOMP_COMPILE_DB, android/botwrecompandroid/app/build,
#   android/probe/app/build, android/botwrecompandroid/build, build) -> the entry's command; else the manual set.
# - covers .c / .cpp / .mm (the ImGui menu is ObjC++ on Android: the Gradle CMake compiles the .mm UI sources as C++).
# Exit codes: 0 all OK; 1 a syntax check failed; 2 unusable invocation / toolchain / input.

set -euo pipefail

R="$(cd "$(dirname "$0")" && pwd)"
NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_NDK:-/opt/android-ndk}}"
CLANG="${LARECOMP_CLANG:-}"
SYSROOT=""

if [ -n "$CLANG" ]; then
  : # (LARECOMP_CLANG: the clang to use, directly)
else
  CLANG="$(find "$NDK_HOME" -maxdepth 6 -name clang -path '*/bin/*' 2>/dev/null | head -n1 || true)"
fi
if [ -z "$CLANG" ]; then CLANG="$(command -v clang || true)"; fi
if [ -z "$CLANG" ]; then
  echo "ERROR: no clang toolchain found (ANDROID_NDK_HOME=$NDK_HOME, LARECOMP_CLANG=$LARECOMP_CLANG)" >&2
  exit 2
fi
if [ ! -x "$CLANG" ]; then
  echo "ERROR: clang not executable: $CLANG" >&2
  exit 2
fi
P="$(cd "$(dirname "$CLANG")" && pwd)"
for i in 1 2 3 4 5; do P="$(dirname "$P")"; done  # clang is .../toolchains/llvm/prebuilt/<triple>/bin/clang -> the NDK root
SYSROOT="$P/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
if [ ! -d "$SYSROOT" ]; then SYSROOT="$(dirname "$P")/toolchains/llvm/prebuilt/linux-x86_64/sysroot"; fi
if [ ! -d "$SYSROOT" ]; then
  echo "ERROR: no NDK sysroot next to $CLANG (looked in $P/toolchains/llvm/prebuilt/linux-x86_64/sysroot)" >&2
  exit 2
fi

IMGUI="$R/third_party/imgui"
BOTW_GEN_DIR="$R/android/botwrecompandroid/app/build/generated/botwrecompandroid/inc"
DEPS="$R/android/deps/out"
INCLUDES=(
  -I"$R/runtime"
  -I"$R/runtime/nvn"
  -I"$IMGUI"
  -I"$IMGUI/backends"
  -I"$DEPS/include"
  -I/opt/sdl3-src/include
  -I/opt/volk-src
  -I/opt/vma-src/include
  -I/opt/shaderc-src/libshaderc/include
  -I"$BOTW_GEN_DIR"
  -I"$R/third_party/smaa/Textures"
  -I"$R/android/common"
)
DEFINES=(
  -DVK_NO_PROTOTYPES
  -DVK_USE_PLATFORM_ANDROID_KHR
  -DIMGUI_IMPL_VULKAN_USE_VOLK
  -DVMA_STATIC_VULKAN_FUNCTIONS=0
  -DVMA_DYNAMIC_VULKAN_FUNCTIONS=1
)

CDB=""
for d in "${LARECOMP_COMPILE_DB:-}" "$R/android/botwrecompandroid/app/build" "$R/android/probe/app/build" "$R/android/botwrecompandroid/build" "$R/build"; do
  if [ -n "$d" ] && [ -f "$d/compile_commands.json" ]; then CDB="$d"; break; fi
done

# check_one <file> -> 0 OK / 1 failed / 3 not in the compile database (then: manual flags)
check_one() {
  local f="$1"
  if [ -n "$CDB" ]; then
    if python3 - "$CDB" "$f" "$CLANG" > /tmp/check_cc_out 2>&1 <<'PY'
import json, shlex, subprocess, sys
cdb, f, clang = sys.argv[1], sys.argv[2], sys.argv[3]
db = json.load(open(cdb))
ent = None
for e in db:
    ef = (e.get('file') or '').replace('\\', '/')
    ff = f.replace('\\', '/')
    if ef == ff or ef.endswith('/' + ff) or ff.endswith('/' + ef):
        ent = e
        break
if ent is None:
    sys.exit(3)
cmd = shlex.split(ent.get('command') or '')
if cmd and (cmd[0].endswith('clang++') or cmd[0].endswith('clang++_') or 'clang++' in cmd[0]):
    cmd[0] = clang
else:
    cmd[0] = clang
out_i = cmd.index('-c') if '-c' in cmd else None
if out_i is not None:
    del cmd[out_i:out_i + 2]  # "-c <obj>": syntax-only instead
if '-o' in cmd:
    o = cmd.index('-o')
    del cmd[o:o + 2]
r = subprocess.run(cmd, cwd=(ent.get('directory') or '.'), capture_output=True, text=True)
sys.stdout.write(r.stdout)
sys.stderr.write(r.stderr)
sys.stdout.flush()
sys.stderr.flush()
sys.exit(r.returncode)
PY
    then
      cat /tmp/check_cc_out
      return 0
    fi
  fi
  if [[ "$f" == *.mm ]]; then
    # (the Gradle CMake compiles the .mm UI sources as C++: -xc++)
    clang --target=aarch64-none-linux-android30 --sysroot="$SYSROOT" -x c++ -fsyntax-only "${INCLUDES[@]}" "${DEFINES[@]}" "$f"
  else
    clang --target=aarch64-none-linux-android30 --sysroot="$SYSROOT" -fsyntax-only "${INCLUDES[@]}" "${DEFINES[@]}" "$f"
  fi
}

CHECK_FILES=()
if [ $# -gt 0 ]; then
  for f in "$@"; do
    case "$f" in
      *.c|*.cpp|*.mm)
        if [ ! -f "$f" ]; then echo "ERROR: check.sh: no such file: $f" >&2; exit 2; fi
        CHECK_FILES+=("$f");;
      *)
        echo "SKIP (not a C/C++/ObjC++ source): $f" >&2
        ;;
    esac
  done
else
  mapfile -t FILES < <(git diff --name-only HEAD; git diff --name-only --cached)
  FILES=($(printf "%s\n" "${FILES[@]}" | sort -u))
  for f in "${FILES[@]}"; do
    case "$f" in
      *.c|*.cpp|*.mm)
        # Skip deleted files
        [ -f "$f" ] && CHECK_FILES+=("$f");;
    esac
  done
fi

if [ ${#CHECK_FILES[@]} -eq 0 ]; then
  echo "ERROR: check.sh: no usable C/C++/ObjC++ input (no modified source file, or none of them is .c/.cpp/.mm)" >&2
  exit 2
fi

FAILED=0
MISSING_HEADERS=()
for f in "${CHECK_FILES[@]}"; do
  echo -n "  $f: "
  if check_one "$f"; then
    echo "OK"
  else
    echo "FAILED"
    FAILED=1
  fi
done

if [ $FAILED -eq 1 ]; then
  echo "ERROR: syntax check failed"
  exit 1
fi
echo "All OK"
exit 0
