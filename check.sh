#!/bin/bash
# check.sh - syntax check for modified C files using clang with Android NDK sysroot
# Usage: ./check.sh [files...] (if no args, checks git diff --name-only HEAD and --cached)

set -euo pipefail

export ANDROID_NDK_HOME=/opt/android-ndk
SYSROOT="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/sysroot"
R="/public/Zelda-botw"
DEPS="$R/android/deps/out"
IMGUI="$R/third_party/imgui"
BOTW_GEN_DIR="$R/android/botwrecompandroid/app/build/generated/botwrecompandroid/inc"

# Base include paths and defines from CMakeLists.txt
INCLUDES=(
  -I"$R/runtime"
  -I"$R/runtime/nvn"
  -I"$IMGUI"
  -I"$IMGUI/backends"
  -I"$DEPS/include"
  -I"$DEPS/volk"
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

# Get files to check
FILES=()
if [ $# -gt 0 ]; then
  FILES=("$@")
else
  mapfile -t FILES < <(git diff --name-only HEAD; git diff --name-only --cached)
  # Deduplicate
  FILES=($(printf "%s\n" "${FILES[@]}" | sort -u))
fi

# Filter to .c and .cpp files
CHECK_FILES=()
for f in "${FILES[@]}"; do
  case "$f" in
    *.c|*.cpp) CHECK_FILES+=("$f") ;;
  esac
done

if [ ${#CHECK_FILES[@]} -eq 0 ]; then
  echo "No C/C++ files to check"
  exit 0
fi

echo "Checking ${#CHECK_FILES[@]} file(s)..."

FAILED=0
SDL3_MISSING=0
for f in "${CHECK_FILES[@]}"; do
  echo -n "  $f: "
  # Check if file includes SDL3
  if grep -q '#include <SDL3/' "$f" 2>/dev/null; then
    if [ ! -d "$DEPS/include/SDL3" ]; then
      echo "SKIPPED (SDL3 headers not built - run android/deps/build_deps.sh or build in CI)"
      SDL3_MISSING=1
      continue
    fi
  fi
  # Use clang exit code (0=OK, non-zero=error)
  if clang --target=aarch64-none-linux-android30 --sysroot="$SYSROOT" \
      "${INCLUDES[@]}" "${DEFINES[@]}" \
      -fsyntax-only "$f" 2>&1 >/dev/null; then
    echo "OK"
  else
    echo "FAILED"
    clang --target=aarch64-none-linux-android30 --sysroot="$SYSROOT" \
      "${INCLUDES[@]}" "${DEFINES[@]}" \
      -fsyntax-only "$f" 2>&1 | head -20
    FAILED=1
  fi
done

if [ $SDL3_MISSING -eq 1 ]; then
  echo "NOTE: Some files skipped due to missing SDL3 headers (build deps in CI)"
fi

if [ $FAILED -eq 1 ]; then
  echo "ERROR: syntax check failed"
  exit 1
else
  echo "All OK"
  exit 0
fi
